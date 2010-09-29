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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "libpscnv.h"
#include "libpscnv_ib.h"

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

struct pscnv_ib_bo *in;
struct pscnv_ib_bo *out;
struct pscnv_ib_bo *cp;
struct pscnv_ib_chan *chan;

int bytes, ints, threads, ctas;

void init(int *drm_fd) {
	int fd, err, ret;
	int chan_id;

        fd = drmOpen("pscnv", 0);
	if (fd == -1) {
		printf ("failed to open drm");
		exit(1);
	}
	
	*drm_fd = fd;
	err = pscnv_ib_chan_new(fd, 0, &chan, 0xdeadbeef, 0, 0);
	if (err < 0){
		printf ("chan: %s\n", strerror(-err));
		exit(1);
	}

	ret = pscnv_obj_eng_new(fd, chan->cid, 0xdeadd00d, 0x50c0, 0);
	if (ret != 0) {
		printf ("tesla: %s\n", strerror(-ret));
		exit(1);
	}

	if (err = pscnv_ib_bo_alloc(fd, chan->vid, 0x1, PSCNV_GEM_VRAM_SMALL | PSCNV_GEM_MAPPABLE, 0, bytes + 0x4000, 0, &in)) {
		printf ("in: %s\n", strerror(-err));
		exit(1);
	}

	if (err = pscnv_ib_bo_alloc(fd, chan->vid, 0x1, PSCNV_GEM_VRAM_SMALL | PSCNV_GEM_MAPPABLE, 0, bytes + 0x4000, 0, &out)) {
		printf ("out: %s\n", strerror(-err));
		exit(1);
	}

	if (err = pscnv_ib_bo_alloc(fd, chan->vid, 0x1, PSCNV_GEM_VRAM_SMALL | PSCNV_GEM_MAPPABLE, 0, 4096, 0, &cp)) {
		printf ("cp: %s\n", strerror(-err));
		exit(1);
	}

	memcpy(cp->map, cpcode, CPSZ);

/* from which offset of pushbuf, size of command and find free slot by nouveau_dma_wait and submit by nvchan_wr32(chan, 0x8c, chan->dma.ib_put); */
/* write batchbuffer don't forget to fire at the end */

	/* nouveau_pushbufs_alloc */

	BEGIN_RING50(chan, 0, 0, 1);
	OUT_RING(chan, 0xdeadd00d);

//	BEGIN_RING50(chan, 0, 0x180, 1); // DMA_NOTIFY
//	OUT_RING(chan, (subc << 13) | (1 << 18) | 0x180);
//	OUT_RING(chan, notify->handle);

	BEGIN_RING50(chan, 0, 0x1a0, 1); // DMA_GLOBAL
	OUT_RING(chan, 0xdeadbeef);

	BEGIN_RING50(chan, 0, 0x1c0, 1); // DMA_CODE_CB
	OUT_RING(chan, 0xdeadbeef);

	BEGIN_RING50(chan, 0, 0x2b8, 1); // enable all lanes
	OUT_RING(chan, 0x1);
	BEGIN_RING50(chan, 0, 0x3b8, 1);
	OUT_RING(chan, 0x2);
	/* nouveau_pushbufs_submit */
	FIRE_RING(chan);

}

struct timeval tvb, tve;

void prepare_mem(int fd) {
	int err;
	uint32_t* test;
/*
	if (err = nouveau_bo_map(in, NOUVEAU_BO_RD|NOUVEAU_BO_WR)) {
		printf ("mapin: %s\n", strerror(-err));
		exit(1);
	}
	memset (in->map, 1, bytes);
	nouveau_bo_unmap (in);
*/
	memset(in->map, 1, bytes);
/*
	if (err = nouveau_bo_map(out, NOUVEAU_BO_RD|NOUVEAU_BO_WR)) {
		printf ("mapout: %s\n", strerror(-err));
		exit(1);
	}
	memset (out->map, 0, bytes);
	nouveau_bo_unmap (out);
*/
	memset(out->map, 0, bytes);
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

	intptr = malloc(bytes);
	memcpy(intptr, out->map, bytes);
	
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

	BEGIN_RING50(chan, 0, 0x210, 2);
//	OUT_RELOC(chan, cp, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, cp, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, cp->vm_base >> 32);
	OUT_RING(chan, cp->vm_base);

	BEGIN_RING50(chan, 0, 0x2b4, 1);
	OUT_RING(chan, threads);	// THREADS_PER_BLOCK

	BEGIN_RING50(chan, 0, 0x2c0, 1);
	OUT_RING(chan, CPREGS);

	BEGIN_RING50(chan, 0, 0x3a4, 5);
	OUT_RING(chan, 0x00010000 | ctas);	// GRIDDIM
	OUT_RING(chan, 0x40);			// SHARED_SIZE
	OUT_RING(chan, 0x10000 | threads);	// BLOCKDIM_XY
	OUT_RING(chan, 0x1);			// BLOCKDIM_Z
	OUT_RING(chan, 0);			// CP_START_ID

	BEGIN_RING50(chan, 0, 0x400, 5);	// input segment
//	OUT_RELOC(chan, in, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, in, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, in->vm_base >> 32);
	OUT_RING(chan, in->vm_base);
	OUT_RING(chan, 0x00000);
	OUT_RING(chan, 0xfffffff);
	OUT_RING(chan, 1);

	BEGIN_RING50(chan, 0, 0x420, 5);	// output segment
//	OUT_RELOC(chan, out, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, out, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, out->vm_base >> 32);
	OUT_RING(chan, out->vm_base);
	OUT_RING(chan, 0x00000);
	OUT_RING(chan, 0xfffffff);
	OUT_RING(chan, 1);

	BEGIN_RING50(chan, 0, 0x374, 1);	// USER_PARAM_COUNT
	OUT_RING(chan, 1 << 8);

	BEGIN_RING50(chan, 0, 0x600, 1);	// USER_PARAM
	OUT_RING(chan, bytes/4);

	BEGIN_RING50(chan, 0, 0x3b4, 1);	// CP_START_ID
	OUT_RING(chan, START_STRIDED);

	BEGIN_RING50(chan, 0, 0x2f8, 1);
	OUT_RING(chan, 1);		// latch BLOCKDIM

	BEGIN_RING50(chan, 0, 0x368, 1);
	OUT_RING(chan, 0);		// LAUNCH

	BEGIN_RING50(chan, 0, 0x50, 1);
	OUT_RING(chan, 0x1);		// LAUNCH

	FIRE_RING(chan);

	while (chan->chmap[0x48/4] != 1);

	check_mem(fd);
}

void lineartest(int fd) {
	printf ("Trying linear access... ");
	prepare_mem(fd);

	BEGIN_RING50(chan, 0, 0x210, 2);
//	OUT_RELOC(chan, cp, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, cp, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, cp->vm_base >> 32);
	OUT_RING(chan, cp->vm_base);

	BEGIN_RING50(chan, 0, 0x2b4, 1);
	OUT_RING(chan, 1);		// THREADS_PER_BLOCK

	BEGIN_RING50(chan, 0, 0x2c0, 1);
	OUT_RING(chan, CPREGS);

	BEGIN_RING50(chan, 0, 0x3a4, 5);
	OUT_RING(chan, 0x00010000 | ctas);	// GRIDDIM
	OUT_RING(chan, 0x40);			// SHARED_SIZE
	OUT_RING(chan, 0x10001);		// BLOCKDIM_XY
	OUT_RING(chan, 0x1);			// BLOCKDIM_Z
	OUT_RING(chan, 0);			// CP_START_ID

	BEGIN_RING50(chan, 0, 0x400, 5);	// input segment
//	OUT_RELOC(chan, in, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, in, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RD | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, in->vm_base >> 32);
	OUT_RING(chan, in->vm_base);
	OUT_RING(chan, 0x00000);
	OUT_RING(chan, 0xfffffff);
	OUT_RING(chan, 1);

	BEGIN_RING50(chan, 0, 0x420, 5);	// output segment
//	OUT_RELOC(chan, out, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR | NOUVEAU_BO_HIGH, 0, 0);
//	OUT_RELOC(chan, out, 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_WR | NOUVEAU_BO_LOW, 0, 0);
	OUT_RING(chan, out->vm_base >> 32);
	OUT_RING(chan, out->vm_base);
	OUT_RING(chan, 0x00000);
	OUT_RING(chan, 0xfffffff);
	OUT_RING(chan, 1);

	BEGIN_RING50(chan, 0, 0x374, 1);	// USER_PARAM_COUNT
	OUT_RING(chan, 2 << 8);

	int stride = ( ints % ctas == 0 ) ? ints / ctas : (ints / ctas) + 1;

	BEGIN_RING50(chan, 0, 0x600, 2);	// USER_PARAM
	OUT_RING(chan, bytes);
	OUT_RING(chan, stride);

	BEGIN_RING50(chan, 0, 0x3b4, 1);	// CP_START_ID
	OUT_RING(chan, START_LINEAR);

	BEGIN_RING50(chan, 0, 0x2f8, 1);
	OUT_RING(chan, 1);		// latch BLOCKDIM

	BEGIN_RING50(chan, 0, 0x368, 1);
	OUT_RING(chan, 0);		// LAUNCH

	BEGIN_RING50(chan, 0, 0x50, 1);
	OUT_RING(chan, 2);

	FIRE_RING(chan);

	while (chan->chmap[0x48/4] != 2);

	check_mem(fd);
}
int main(int argc, char **argv) {
	int c;
	int fd;
//	bytes = 1000000;
	bytes = 245 * 4096;
	bytes = 16 * 4096;
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

