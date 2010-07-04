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
#include <xf86drm.h>
#include <string.h>
#include <stdio.h>
#include "libpscnv.h"
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

	uint32_t size = 0x2000;
	uint32_t handle;
	uint64_t map_handle;
	ret = pscnv_gem_new(fd, 0xf1f0c0de, 0, 0, size, 0, &handle, &map_handle);
	if (ret) {
		printf("new: failed ret = %d\n", ret);
		return 1;
	}
	printf("new: handle %d map %llx\n", handle, map_handle);

	uint32_t *pb_map = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_handle);
	printf ("Mapped at %p\n", pb_map);

	uint32_t vid;
	ret = pscnv_vspace_new(fd, &vid);
	if (ret) {
		printf("vnew: failed ret = %d\n", ret);
		return 1;
	}
	printf ("VID %d\n", vid);

	uint32_t cid;
	uint64_t ch_map_handle;
	ret = pscnv_chan_new(fd, vid, &cid, &ch_map_handle);
	if (ret) {
		printf("cnew: failed ret = %d\n", ret);
		return 1;
	}
	printf ("CID %d %llx\n", cid, ch_map_handle);

	uint64_t offset;
	ret = pscnv_vspace_map(fd, vid, handle, 0x1000, 1ull << 32, 1, 0, &offset);
	if (ret) {
		printf("vmap: failed ret = %d\n", ret);
		return 1;
	}
	printf ("vmap at %llx\n", offset);

	ret = pscnv_obj_vdma_new(fd, cid, 0xdead, 0x3d, 0, offset, 0x1000);
	if (ret) {
		printf("vdnew: failed ret = %d\n", ret);
		return 1;
	}

	ret = pscnv_fifo_init(fd, cid, 0xdead, 0, 1, 0);
	if (ret) {
		printf("fifo_init: failed ret = %d\n", ret);
		return 1;
	}

	volatile uint32_t *chmap = mmap(0, 0x2000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ch_map_handle);
	printf ("Mapped at %p\n", chmap);

	pb_map[0] = 0x40080;
	pb_map[1] = 0xdeadbeef;
	pb_map[2] = 1;
	chmap[0x40/4] = 0x124;

	pause();


        close (fd);
        close (fd2);
        
        return 0;
}
