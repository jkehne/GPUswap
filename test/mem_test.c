/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 PathScale Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "mem_test.h"

#define CPREGS 4
#define START_STRIDED 0
#define START_LINEAR 0x50
uint32_t cpcode[] = {
	0x11002218,		// mov b16 $r3l u16 s[0x2]		// gets ntid.x to $r3l
	0x61062c00, // add $r0 mul u16 u16 s[0xc] $r3l $r0	// gets tid.x + ctaid.x * ntid.x to $r0, our loop counter [tid.x is always at $r0 at CP start]
	0x3000c9fd, 0x6420c7c8, // set le u32 $c0 # b32 s[0x10] $r0	// if size <= counter, set $c0
	0x30000003, 0x00000280, // lg $c0 ret				// if size <= counter, exit
	// loop:
	0x30020005, 0xc4100780, // shl b32 $r1 $r0 0x2			// counter*4 to $r1
	0xd0000209, 0x80c00780, // mov b32 $r2 g0[$r1]			// read from input area
	0xd0010209, 0xa0c00780, // mov b32 g1[$r1] $r2			// write to output area
	0x60064801, 0x00200780, // add $r0 mul u16 u16 s[0x8] $r3l $r0	// counter += ntid.x * nctaid.x
	0x3000c9fd, 0x642107c8, // set gt u32 $c0 # b32 s[0x10] $r0	// if size > counter, set $c0
	0x10003003, 0x00000280, // lg $c0 bra 0x18			// if size > counter, loop
	0x30000003, 0x00000780, // ret					// exit program.

	0x1100ea00,		// mov b32 $r0 b32 s[0x14]		// gets stride to $r0
	0x41002c04,		// mul $r1 u16 u16 s[0xc] u16 $r0l
	0x41012c08,		// mul $r2 u16 u16 s[0xc] u16 $r0h
	0x2004060c,		// add b16 $r1h $r1h $r2l		// multiplies stride by ctaid.x and puts result into $r1, our loop counter
	0x20000009, 0x04004780,	// add b32 $r2 $r0 $r1			// stride*ctaid.x + stride into $r2
	0x3002c809, 0xa4200780, // min u32 $r2 b32 s[0x10] $r2		// min (stride*ctaid.x + stride, size) into $r2, the upper bound of our loop
	0x300203fd, 0x640187c8, // set ge u32 $c0 # $r1 $r2		// set $c0 if counter >= max
	0x30000003, 0x00000280, // lg $c0 ret				// finish execution if counter >= max
	// loop:
	0x3002020d, 0xc4100780, // shl b32 $r3 $r1 0x2			// counter*4 to $r3
	0xd0000601, 0x80c00780, // mov b32 $r0 g0[$r3]			// read from input area
	0xd0010601, 0xa0c00780, // mov b32 g1[$r3] $r0			// write to output area
	0x20018205, 0x00000003, // add b32 $r1 $r1 0x1			// counter++
	0x300203fd, 0x640047c8, // set lt u32 $c0 # $r1 $r2		// set $c0 if counter < max
	0x10010003, 0x00000280, // lg $c0 bra 0x80			// and branch back to loop if so
	0x30000003, 0x00000780, // ret					// exit program.
};


#define CPSZ sizeof(cpcode)

struct nouveau_object *in;
struct nouveau_object *out;
struct nouveau_object *cp;
struct nouveau_chan *chan;
struct nouveau_grobj *turing;
struct nouveau_notifier *notify;
struct nouveau_object *pushbuf;
struct drm_nouveau_pscmm_exec_object * exec_objects;
int exec_buf_nr = 0;

int bytes, ints, threads, ctas;

static void
BEGIN_RING(struct nouveau_chan *chan, struct nouveau_grobj *gr,
           unsigned mthd, unsigned size);

int drm_open_any(void)
{
	char name[20];
	int i, fd;
	
	for (i = 0; i < 16; i++) {
		sprintf(name, "/dev/fbs/drm%d", i);
		fd = open(name, O_RDWR);
		if (fd != -1)
			return fd;
	}
	return -1;
}

int
nouveau_grobj_ref(struct nouveau_chan *chan, uint32_t handle,
		  struct nouveau_grobj **grobj)
{
	struct nouveau_grobj *nvgrobj;

	nvgrobj = calloc(1, sizeof(struct nouveau_grobj));
	if (!nvgrobj)
		return -ENOMEM;
	nvgrobj->channel = chan;
	nvgrobj->handle = handle;
	nvgrobj->grclass = 0;

	*grobj = nvgrobj;
	return 0;
}

int nouveau_channel_alloc(int fd, uint32_t fb_ctxdma, uint32_t tt_ctxdma, struct nouveau_chan **chan)
{
	struct drm_nouveau_channel_alloc nv_chan;
	struct nouveau_chan *channel;
	int ret, i;

	nv_chan.fb_ctxdma_handle = fb_ctxdma;
	nv_chan.tt_ctxdma_handle = tt_ctxdma;
	
	ret = ioctl(fd, DRM_IOCTL_NOUVEAU_CHANNEL_ALLOC, &nv_chan);
	if (ret < 0) {
		printf("alloc channel failed\n");
//		free(channel);
		return ret;
	}
	channel = calloc(1, sizeof(struct nouveau_chan));

	channel->fd = fd;
	channel->id = nv_chan.channel;
	channel->pushbuf_domains =  nv_chan.pushbuf_domains;

	if (nouveau_grobj_ref(channel, fb_ctxdma,
			      &channel->vram) ||
	    nouveau_grobj_ref(channel, tt_ctxdma,
		    	      &channel->gart)) {
		nouveau_channel_free(channel);
		return -EINVAL;
	}

	/* Mark all DRM-assigned subchannels as in-use */
	for (i = 0; i < nv_chan.nr_subchan; i++) {
		struct nouveau_grobj *gr = calloc(1, sizeof(*gr));

		gr->bound = NOUVEAU_GROBJ_BOUND_EXPLICIT;
		gr->subc = i;
		gr->handle = nv_chan.subchan[i].handle;
		gr->grclass = nv_chan.subchan[i].grclass;
		gr->channel = channel;

		channel->subc[i].gr = gr;
	}

	/* notify buf*/
	/* pushbuf */
	*chan = channel;

	return 0;

}

int nouveau_channel_free(struct nouveau_chan *chan)
{
	struct drm_nouveau_channel_free cfree;
        int ret;
	cfree.channel = chan->id;

        ret = ioctl(chan->fd, DRM_IOCTL_NOUVEAU_CHANNEL_ALLOC, &cfree);
        if (ret < 0) {
                printf("free channel failed\n");
                return ret;
        }
	free(chan);
        return 0;

}

int nouveau_grobj_alloc(int fd, struct nouveau_chan *chan, uint32_t handle,
			int class, struct nouveau_grobj **grobj)
{
	int ret;
	struct drm_nouveau_grobj_alloc new_grobj;
	struct nouveau_grobj *gr;

	gr = calloc(1, sizeof(struct nouveau_grobj));	

	new_grobj.channel = chan->id;
	new_grobj.handle = handle;
	new_grobj.class = class;
	ret = ioctl(fd, DRM_IOCTL_NOUVEAU_GROBJ_ALLOC, &new_grobj);
        if (ret < 0) {
                printf("nouveau_grobj_alloc failed\n");
		free(gr);
                return ret;
        }

	gr->channel = chan;
	gr->handle  = handle;
	gr->grclass = class;
	gr->bound   = NOUVEAU_GROBJ_UNBOUND;
	gr->subc    = -1;

	*grobj = gr;
	return ret;
}

int nouveau_grobj_free(int fd, int chanid, struct nouveau_grobj *grobj)
{
	struct drm_nouveau_gpuobj_free objfree;
        int ret;

        objfree.channel = chanid;
        objfree.handle = grobj->handle;
        ret = ioctl(fd, DRM_IOCTL_NOUVEAU_GPUOBJ_FREE, &objfree);
        if (ret < 0) {
                printf("nouveau_grobj_free failed\n");
                return ret;
        }

	free(grobj);
        return ret;
}

int nouveau_notifier_alloc(int fd, struct nouveau_chan *chan, uint32_t handle,
			uint32_t size, struct nouveau_notifier ** notify)
{
	struct drm_nouveau_notifierobj_alloc na;
	struct nouveau_notifier *notifier;
        int ret;

	notifier = calloc(1, sizeof(struct nouveau_notifier));
        na.channel = chan->id;
        na.handle = handle;
        na.size = size;
        ret = ioctl(fd, DRM_IOCTL_NOUVEAU_NOTIFIEROBJ_ALLOC, &na);
        if (ret < 0) {
                printf("alloc notifier failed\n");
		free(notifier);
                return 0;
        }

	notifier->channel = chan;
	notifier->handle = handle;
	notifier->size = size;
	notifier->offset = na.offset;

	*notify = notifier;
        return ret;
}

int
nouveau_bo_new_tile(int fd, uint32_t size,
	uint32_t placement, struct nouveau_object **nv_object)
{
	struct drm_nouveau_pscmm_new bo; 
	struct drm_nouveau_pscmm_move bo_move;
	struct nouveau_object *object;
        int ret;

	object = calloc(1, sizeof(struct nouveau_object));

        bo.size = size;
        ret = ioctl(fd, DRM_IOCTL_NOUVEAU_PSCMM_NEW, &bo);

        if (ret < 0) {
                printf("alloc bo gem failed\n");
		free(object);
                return ret;
        }
	object->size = size;
	object->handle = bo.handle;

	if ((placement & NOUVEAU_PSCMM_DOMAIN_VRAM) || (placement & NOUVEAU_PSCMM_DOMAIN_GART)) {
	        bo_move.handle = object->handle;
	      	bo_move.old_domain = NOUVEAU_PSCMM_DOMAIN_CPU;
	        bo_move.new_domain = placement;
		ret = ioctl(fd, DRM_IOCTL_NOUVEAU_PSCMM_MOVE, &bo_move);	
	        if (ret < 0) {
	                printf("move bo failed %d \n", ret);
			free(object);
	                return ret;
	        }

              object->placement = placement;
		object->offset = bo_move.presumed_offset;
		object->placement = bo_move.presumed_domain;
	}
	*nv_object = object;

	return ret;
}

int
nouveau_bo_write(int fd, uint64_t size, uint64_t offset, uintptr_t data_ptr, struct nouveau_object *nv_object)
{
	struct nouveau_pscmm_write bo_write;
	int ret;

	bo_write.handle = nv_object->handle;
	bo_write.tile_mode = nv_object->tile_mode;
	bo_write.size = size;
	bo_write.offset = offset;
	bo_write.data_ptr = data_ptr;

	ret = ioctl(fd, DRM_IOCTL_NOUVEAU_PSCMM_WRITE, &bo_write);
	if (ret < 0) {
		printf("write bo failed %d \n", ret);
		return ret;
	}

	return ret;
}

int
nouveau_bo_read(int fd, uint64_t size, uint64_t offset, uintptr_t data_ptr, struct nouveau_object *nv_object)
{
        struct nouveau_pscmm_read bo_read;
        int ret;

        bo_read.handle = nv_object->handle;
        bo_read.tile_mode = nv_object->tile_mode;
        bo_read.size = size;
        bo_read.offset = offset;
        bo_read.data_ptr = data_ptr;

        ret = ioctl(fd, DRM_IOCTL_NOUVEAU_PSCMM_READ, &bo_read);
        if (ret < 0) {
                printf("read bo failed %d \n", ret);
                return ret;
        }

        return ret;
}

int
nouveau_bo_chan_map(int fd, uint32_t chanid, uint32_t low, struct nouveau_object *nv_object)
{
        struct drm_nouveau_pscmm_chanmap chanmap;
        int ret;

        chanmap.handle = nv_object->handle;
        chanmap.tile_flags = nv_object->tile_flags;
        chanmap.channel = chanid;
        chanmap.low = low;

        ret = ioctl(fd, DRM_NOUVEAU_PSCMM_CHAN_MAP, &chanmap);
        if (ret < 0) {
                printf("chan map failed %d \n", ret);
                return ret;
        }
	nv_object->channel = chanid;
        nv_object->chan_map = chanmap.addr_ptr;

        return ret;
}

int
nouveau_bo_chan_unmap(int fd, struct nouveau_object *nv_object)
{
        struct drm_nouveau_pscmm_chanunmap chanunmap;
        int ret;

        chanunmap.handle = nv_object->handle;
        chanunmap.channel = nv_object->channel;

        ret = ioctl(fd, DRM_NOUVEAU_PSCMM_CHAN_UNMAP, &chanunmap);
        if (ret < 0) {
                printf("chan unmap failed %d \n", ret);
                return ret;
        }
        nv_object->channel = 0;
        nv_object->chan_map = NULL;

        return ret;
}

int
nouveau_bo_map(int fd, struct nouveau_object *nv_object)
{
        struct nouveau_pscmm_mmap bomap;
        int ret;

        bomap.handle = nv_object->handle;
        bomap.tile_flags = nv_object->tile_flags;
        bomap.size= nv_object->size;
        bomap.offset= 0;

        ret = ioctl(fd, DRM_NOUVEAU_PSCMM_MMAP, &bomap);
        if (ret < 0) {
                printf("chan map failed %d \n", ret);
                return ret;
        }
	nv_object->gem_map = (uint32_t *)bomap.addr_ptr;

        return ret;
}

void
nouveau_add_validate_buffer(struct nouveau_object *obj)
{
	exec_buf_nr++;
	exec_objects = realloc(exec_objects,
			    sizeof(*exec_objects) * exec_buf_nr);

	exec_objects[exec_buf_nr - 1].handle = obj->handle;
}

void
nouveau_pushbufs_alloc(struct nouveau_chan *chan, uint32_t size)
{
	nouveau_bo_new_tile(chan->fd, 4096, NOUVEAU_PSCMM_DOMAIN_VRAM, &pushbuf);
	nouveau_bo_map(chan->fd, pushbuf);
	pushbuf->remaining = (size - 8) / 4;
	chan->pushbuf = pushbuf;
}

void
nouveau_pushbufs_submit(struct nouveau_chan *chan)
{
	struct drm_nouveau_pscmm_exec execbuf;
	int ret;

	nouveau_add_validate_buffer(chan->pushbuf);
	exec_objects[exec_buf_nr - 1].nr_dwords = (chan->pushbuf->size - 8) / 4 - chan->pushbuf->remaining;
	execbuf.channel = chan->id;
	execbuf.buffer_count = exec_buf_nr;
	execbuf.buffers_ptr = exec_objects;
	
        ret = ioctl(chan->fd, DRM_NOUVEAU_PSCMM_EXEC, &execbuf);
        if (ret < 0) {
                printf("pushbufs_submit failed %d \n", ret);
                return;
        }
	free(chan->pushbuf);
	chan->pushbuf = NULL;
}

static void
FIRE_RING(struct nouveau_chan *chan)
{
	nouveau_pushbufs_submit(chan);
}

static void
OUT_RING(struct nouveau_chan *chan, unsigned data)
{
	*(chan->pushbuf->gem_map++) = (data);
}

void
nouveau_grobj_autobind(struct nouveau_grobj *grobj)
{
	struct nouveau_subchannel *subc = NULL;
	int i;

	for (i = 0; i < 8; i++) {
		struct nouveau_subchannel *scc = &grobj->channel->subc[i];

		if (scc->gr && scc->gr->bound == NOUVEAU_GROBJ_BOUND_EXPLICIT)
			continue;

		if (!subc || scc->sequence < subc->sequence)
			subc = scc;
	}

	if (subc->gr) {
		subc->gr->bound = NOUVEAU_GROBJ_UNBOUND;
		subc->gr->subc  = -1;
	}

	subc->gr = grobj;
	subc->gr->bound = NOUVEAU_GROBJ_BOUND;
	subc->gr->subc  = subc - &grobj->channel->subc[0];

	BEGIN_RING(grobj->channel, grobj, 0x0000, 1);
	OUT_RING  (grobj->channel, grobj->handle);
}

static void
BEGIN_RING(struct nouveau_chan *chan, struct nouveau_grobj *gr,
	   unsigned mthd, unsigned size)
{
	if (gr->bound == NOUVEAU_GROBJ_UNBOUND)
		nouveau_grobj_autobind(gr);
	chan->subc[gr->subc].sequence = chan->subc_sequence++;

	OUT_RING(chan, (gr->subc << 13) | (size << 18) | mthd);
	chan->pushbuf->remaining -= (size + 1);
}

void init(int *drm_fd) {
	int fd, err, ret;
	int chan_id;
	uint32_t notifier_offset;

	fd = drm_open_any();	// open device
	if (fd == -1) {
		printf ("failed to open drm");
		exit(1);
	}
	
	*drm_fd = fd;
	err = nouveau_channel_alloc(fd, 0xdeadbeef, 0xbeefdead, &chan); // open FIFO
	if (err < 0){
		printf ("chan: %s\n", strerror(-err));
		exit(1);
	}

	ret = nouveau_grobj_alloc(fd, chan, 0xdeadd00d, 0x50c0, &turing); // create the COMPUTE object
	if (ret != 0) {
		printf ("tesla: %s\n", strerror(-ret));
		exit(1);
	}

	if (err = nouveau_bo_new_tile(fd, bytes,
		NOUVEAU_PSCMM_DOMAIN_VRAM, &in)) {	// allocate input BO
		printf ("in: %s\n", strerror(-err));
		exit(1);
	}

	if (err = nouveau_bo_new_tile(fd, bytes,
		NOUVEAU_PSCMM_DOMAIN_VRAM, &out)) {	// allocate output BO
		printf ("out: %s\n", strerror(-err));
		exit(1);
	}

	if (err = nouveau_bo_new_tile(fd, 4096,
		NOUVEAU_PSCMM_DOMAIN_VRAM, &cp)) { // allocate code segment
		printf ("cp: %s\n", strerror(-err));
		exit(1);
	}

	err = nouveau_notifier_alloc(fd, chan, 0xbeef0301, 1, &notify); 	// create notifier
	if (err < 0) {
		printf ("notify err\n");
		exit(1);
	}

	if (err = nouveau_bo_write(fd, CPSZ, 0, (uintptr_t)cpcode, cp)) {
		printf ("write: %s\n", strerror(-err));
		exit(1);
	}

/* from which offset of pushbuf, size of command and find free slot by nouveau_dma_wait and submit by nvchan_wr32(chan, 0x8c, chan->dma.ib_put); */
/* write batchbuffer don't forget to fire at the end */

	/* nouveau_pushbufs_alloc */
	nouveau_pushbufs_alloc(chan, 4096);

	BEGIN_RING(chan, turing, 0x180, 1); // DMA_NOTIFY
//	OUT_RING(chan, (subc << 13) | (1 << 18) | 0x180);
	OUT_RING(chan, notify->handle);

	BEGIN_RING(chan, turing, 0x1a0, 1); // DMA_GLOBAL
	OUT_RING(chan, chan->vram->handle);

	BEGIN_RING(chan, turing, 0x1c0, 1); // DMA_CODE_CB
	OUT_RING(chan, chan->vram->handle);

	BEGIN_RING(chan, turing, 0x2b8, 1); // enable all lanes
	OUT_RING(chan, 0x1);
	BEGIN_RING(chan, turing, 0x3b8, 1);
	OUT_RING(chan, 0x2);

	/* nouveau_pushbufs_submit */
	FIRE_RING(chan);
}

struct timeval tvb, tve;

void prepare_mem(int fd) {
	int err;
	uint32_t *test;
/*
	if (err = nouveau_bo_map(in, NOUVEAU_BO_RD|NOUVEAU_BO_WR)) {
		printf ("mapin: %s\n", strerror(-err));
		exit(1);
	}
	memset (in->map, 1, bytes);
	nouveau_bo_unmap (in);
*/
	test = calloc(31360, sizeof(uint32_t));
	memset(test, 1, bytes);
	if (err = nouveau_bo_write(fd, bytes, 0, (uintptr_t)test, in)) {
		printf ("write: %s\n", strerror(-err));
		exit(1);
	}
/*
	if (err = nouveau_bo_map(out, NOUVEAU_BO_RD|NOUVEAU_BO_WR)) {
		printf ("mapout: %s\n", strerror(-err));
		exit(1);
	}
	memset (out->map, 0, bytes);
	nouveau_bo_unmap (out);
*/
	memset(test, 0, bytes);
	if (err = nouveau_bo_write(fd, bytes, 0, (uintptr_t)test, out)) {
		printf ("write: %s\n", strerror(-err));
		exit(1);
	}
	free(test);
	gettimeofday(&tvb, 0);
}

void check_mem(int fd) {
	int *intptr, i;
	int err;
/*
	if (err = nouveau_bo_map(out, NOUVEAU_BO_RD|NOUVEAU_BO_WR)) {
		printf ("mapout: %s\n", strerror(-err));
		exit(1);
	}
*/
	gettimeofday(&tve, 0); // we need to get the time after mapping, since it's the sync point with GPU
	double secdiff = tve.tv_sec - tvb.tv_sec;
	secdiff += (tve.tv_usec - tvb.tv_usec) / 1000000.0;
	printf ("\t%fs ", secdiff);

	intptr = calloc(31360, sizeof(uint32_t));
	if (err = nouveau_bo_read(fd, bytes, 0, (uintptr_t)intptr, out)) {
		printf ("write: %s\n", strerror(-err));
		exit(1);
	}
	
//	intptr = out->map;
	for (i = 0; i < ints; i++)
		if (intptr[i] != 0x01010101) {
			printf ("Copy failed at index %d!\n", i);
			return;
		}

//	nouveau_bo_unmap (out);
	printf ("Passed.\n");
}

void stridetest(int fd) {
	printf ("Trying strided access... ");
	prepare_mem(fd);

	nouveau_pushbufs_alloc(chan, 4096);
	
	nouveau_add_validate_buffer(cp);
	BEGIN_RING(chan, turing, 0x210, 2);
//	OUT_RELOC(chan, cp, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, cp, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, cp->offset >> 32);
	OUT_RING(chan, cp->offset);
	

	BEGIN_RING(chan, turing, 0x2b4, 1);
	OUT_RING(chan, threads);	// THREADS_PER_BLOCK

	BEGIN_RING(chan, turing, 0x2c0, 1);
	OUT_RING(chan, CPREGS);

	BEGIN_RING(chan, turing, 0x3a4, 5);
	OUT_RING(chan, 0x00010000 | ctas);	// GRIDDIM
	OUT_RING(chan, 0x40);			// SHARED_SIZE
	OUT_RING(chan, 0x10000 | threads);	// BLOCKDIM_XY
	OUT_RING(chan, 0x1);			// BLOCKDIM_Z
	OUT_RING(chan, 0);			// CP_START_ID

	nouveau_add_validate_buffer(in);
	BEGIN_RING(chan, turing, 0x400, 5);	// input segment
//	OUT_RELOC(chan, in, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, in, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, in->offset >> 32);
	OUT_RING(chan, in->offset);
	OUT_RING(chan, 0x00000);
	OUT_RING(chan, (bytes-1) | 0xff); // alignment...
	OUT_RING(chan, 1);

	nouveau_add_validate_buffer(out);
	BEGIN_RING(chan, turing, 0x420, 5);	// output segment
//	OUT_RELOC(chan, out, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, out, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, out->offset >> 32);
	OUT_RING(chan, out->offset);
	OUT_RING(chan, 0x00000);
	OUT_RING(chan, (bytes-1) | 0xff);
	OUT_RING(chan, 1);

	BEGIN_RING(chan, turing, 0x374, 1);	// USER_PARAM_COUNT
	OUT_RING(chan, 1 << 8);

	BEGIN_RING(chan, turing, 0x600, 1);	// USER_PARAM
	OUT_RING(chan, bytes);

	BEGIN_RING(chan, turing, 0x3b4, 1);	// CP_START_ID
	OUT_RING(chan, START_STRIDED);

	BEGIN_RING(chan, turing, 0x2f8, 1);
	OUT_RING(chan, 1);		// latch BLOCKDIM

	BEGIN_RING(chan, turing, 0x368, 1);
	OUT_RING(chan, 0);		// LAUNCH

	FIRE_RING(chan);

	check_mem(fd);
}

void lineartest(int fd) {
	printf ("Trying linear access... ");
	prepare_mem(fd);

	nouveau_pushbufs_alloc(chan, 4096);
	
	nouveau_add_validate_buffer(cp);
	BEGIN_RING(chan, turing, 0x210, 2);
//	OUT_RELOC(chan, cp, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, cp, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, cp->offset >> 32);
	OUT_RING(chan, cp->offset);

	BEGIN_RING(chan, turing, 0x2b4, 1);
	OUT_RING(chan, 1);		// THREADS_PER_BLOCK

	BEGIN_RING(chan, turing, 0x2c0, 1);
	OUT_RING(chan, CPREGS);

	BEGIN_RING(chan, turing, 0x3a4, 5);
	OUT_RING(chan, 0x00010000 | ctas);	// GRIDDIM
	OUT_RING(chan, 0x40);			// SHARED_SIZE
	OUT_RING(chan, 0x10001);		// BLOCKDIM_XY
	OUT_RING(chan, 0x1);			// BLOCKDIM_Z
	OUT_RING(chan, 0);			// CP_START_ID

	nouveau_add_validate_buffer(in);
	BEGIN_RING(chan, turing, 0x400, 5);	// input segment
//	OUT_RELOC(chan, in, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, in, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, in->offset >> 32);
	OUT_RING(chan, in->offset);
	OUT_RING(chan, 0x00000);
	OUT_RING(chan, bytes-1);
	OUT_RING(chan, 1);

	nouveau_add_validate_buffer(out);
	BEGIN_RING(chan, turing, 0x420, 5);	// output segment
//	OUT_RELOC(chan, out, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, out, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, out->offset >> 32);
	OUT_RING(chan, out->offset);
	OUT_RING(chan, 0x00000);
	OUT_RING(chan, bytes-1);
	OUT_RING(chan, 1);

	BEGIN_RING(chan, turing, 0x374, 1);	// USER_PARAM_COUNT
	OUT_RING(chan, 2 << 8);

	int stride = ( ints % ctas == 0 ) ? ints / ctas : (ints / ctas) + 1;

	BEGIN_RING(chan, turing, 0x600, 2);	// USER_PARAM
	OUT_RING(chan, bytes);
	OUT_RING(chan, stride);

	BEGIN_RING(chan, turing, 0x3b4, 1);	// CP_START_ID
	OUT_RING(chan, START_LINEAR);

	BEGIN_RING(chan, turing, 0x2f8, 1);
	OUT_RING(chan, 1);		// latch BLOCKDIM

	BEGIN_RING(chan, turing, 0x368, 1);
	OUT_RING(chan, 0);		// LAUNCH

	FIRE_RING(chan);

	check_mem(fd);
}
int main(int argc, char **argv) {
	int c;
	int fd;
//	bytes = 1000000;
	bytes = 245 * 4096;
	ctas = 10;
	threads = 128;

	
	while ((c = getopt (argc, argv, "s:c:t:")) != -1)
		switch (c) {
			case 's':
				bytes = atoi(optarg);
				break;
			case 'c':
				ctas = atoi(optarg);
				break;
			case 't':
				threads = atoi(optarg);
				break;
		}

	ints = bytes / sizeof(int);

	init(&fd);


	stridetest(fd);
	lineartest(fd);

	return 0;
}

