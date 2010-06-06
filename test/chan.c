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
#include "drm.h"
#include <xf86drm.h>
#include <string.h>
#include <stdio.h>
#include "pscnv_drm.h"
#include <sys/mman.h>

int
main()
{
	int fd, fd2;
	int result;
	int ret;
	int i;
	
        fd = drmOpen("pscnv", 0);
        fd2 = drmOpen("pscnv", 0);

	if (fd == -1 || fd2 == -1)
		return 1;

	struct drm_pscnv_gem_info info;
	memset (&info, 0, sizeof(info));
	info.handle = 0xdeaddead;
	info.cookie = 0xf1f0c0d;
	info.flags = 0;
	info.tile_flags = 0;
	info.size = 0x2000;
	info.map_handle = 0xdeaddeaddeaddeadull;
	info.user[0] = 0xdeadbeef;
	info.user[1] = 0xcafebabe;

	ret = drmCommandWriteRead(fd, DRM_PSCNV_GEM_NEW, &info, sizeof(info));
	if (ret) {
		printf("new: failed ret = %d\n", ret);
		return 1;
	}
	printf("new: handle %d map %llx\n", info.handle, info.map_handle);

	uint32_t *pb_map = mmap(0, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, info.map_handle);
	printf ("Mapped at %p\n", pb_map);

	struct drm_pscnv_vspace_req req;
	ret = drmCommandWriteRead(fd, DRM_PSCNV_VSPACE_NEW, &req, sizeof(req));
	if (ret) {
		printf("vnew: failed ret = %d\n", ret);
		return 1;
	}
	int vid = req.vid;
	printf ("VID %d\n", vid);

	struct drm_pscnv_chan_new chnew;
	chnew.vid = vid;
	ret = drmCommandWriteRead(fd, DRM_PSCNV_CHAN_NEW, &chnew, sizeof(chnew));
	if (ret) {
		printf("cnew: failed ret = %d\n", ret);
		return 1;
	}
	int cid = chnew.cid;
	printf ("CID %d %llx\n", cid, chnew.map_handle);

	struct drm_pscnv_obj_vdma_new vdnew;
	vdnew.cid = cid;
	vdnew.handle = 0xbeef;
	vdnew.oclass = 0x3d;
	vdnew.flags = 0;
	vdnew.start = 0;
	vdnew.size = 1ull << 40;
	ret = drmCommandWriteRead(fd, DRM_PSCNV_OBJ_VDMA_NEW, &vdnew, sizeof(vdnew));
	if (ret) {
		printf("vdnew: failed ret = %d\n", ret);
		return 1;
	}

	struct drm_pscnv_vspace_map vmap;
	vmap.vid = vid;
	vmap.handle = info.handle;
	vmap.start = 0;
	vmap.end = 1ull << 32;
	vmap.back = 1;
	vmap.flags = 0;
	ret = drmCommandWriteRead(fd, DRM_PSCNV_VSPACE_MAP, &vmap, sizeof(vmap));
	if (ret) {
		printf("vmap: failed ret = %d\n", ret);
		return 1;
	}
	printf ("vmap at %llx\n", vmap.offset);

	vdnew.handle = 0xdead;
	vdnew.start = vmap.offset;
	vdnew.size = 0x1000;
	ret = drmCommandWriteRead(fd, DRM_PSCNV_OBJ_VDMA_NEW, &vdnew, sizeof(vdnew));
	if (ret) {
		printf("vdnew: failed ret = %d\n", ret);
		return 1;
	}

	struct drm_pscnv_fifo_init finit;
	finit.cid = cid;
	finit.pb_handle = 0xbeef;
	finit.flags = 0;
	finit.slimask = 1;
	finit.pb_start = vmap.offset;
	ret = drmCommandWriteRead(fd, DRM_PSCNV_FIFO_INIT, &finit, sizeof(finit));
	if (ret) {
		printf("fifo_init: failed ret = %d\n", ret);
		return 1;
	}

	uint32_t *chmap = mmap(0, 0x2000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, chnew.map_handle);
	printf ("Mapped at %p\n", chmap);

	pb_map[0] = 0x40060;
	pb_map[1] = 0xdead;
	pb_map[2] = 0x40064;
	pb_map[3] = 0x40;
	pb_map[4] = 0x4006c;
	pb_map[5] = 0xdeadbeef;
	pb_map[6] = 0x40050;
	pb_map[7] = 0xcafebabe;
	for (i = 8; i < 0x40; i++)
		pb_map[i] = 0;
	chmap[0x40/4] = vmap.offset+8*4;

	while (chmap[0x48/4] != 0xcafebabe);

	for (i = 0; i < 0x40; i++)
		printf ("%x: %08x\n", i*4, chmap[i]);
	for (i = 0; i < 0x40; i++)
		printf ("%x: %08x\n", i*4, pb_map[i]);

        close (fd);
        close (fd2);
        
        return 0;
}
