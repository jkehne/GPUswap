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

	ret = pscnv_obj_vdma_new(fd, cid, 0xbeef, 0x3d, 0, 0, 1ull << 40);
	if (ret) {
		printf("vdnew: failed ret = %d\n", ret);
		return 1;
	}

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

	ret = pscnv_fifo_init(fd, cid, 0xbeef, 0, 1, offset);
	if (ret) {
		printf("fifo_init: failed ret = %d\n", ret);
		return 1;
	}

	uint32_t *chmap = mmap(0, 0x2000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ch_map_handle);
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
	chmap[0x40/4] = offset+8*4;

	while (chmap[0x48/4] != 0xcafebabe);

	for (i = 0; i < 0x40; i++)
		printf ("%x: %08x\n", i*4, chmap[i]);
	for (i = 0; i < 0x40; i++)
		printf ("%x: %08x\n", i*4, pb_map[i]);

	ret = pscnv_obj_gr_new (fd, cid, 0xbeef39, 0x5039, 0);
	if (ret) {
		printf("gr_new: failed ret = %d\n", ret);
		return 1;
	}
	pb_map[8] = 0x42000;
	pb_map[9] = 0xbeef39;
	pb_map[10] = 0xc2180;
	pb_map[11] = 0xdead;
	pb_map[12] = 0xdead;
	pb_map[13] = 0xdead;
	pb_map[14] = 0x42200;
	pb_map[15] = 0x1;
	pb_map[16] = 0x4221c;
	pb_map[17] = 0x1;
	pb_map[18] = 0x82238;
	pb_map[19] = 0;
	pb_map[20] = 0;
	pb_map[21] = 0x20230c;
	pb_map[22] = 0;
	pb_map[23] = 0x80;
	pb_map[24] = 4;
	pb_map[25] = 8;
	pb_map[26] = 4;
	pb_map[27] = 30;
	pb_map[28] = 0x101;
	pb_map[29] = 0;
	pb_map[30] = 0x40050;
	pb_map[31] = 0xbeefbeef;

	chmap[0x40/4] = offset+32*4;

	while (chmap[0x48/4] != 0xbeefbeef);

	for (i = 0; i < 0x40; i++)
		printf ("%x: %08x\n", i*4, chmap[i]);
	for (i = 0; i < 0x40; i++)
		printf ("%x: %08x\n", i*4, pb_map[i]);

        close (fd);
        close (fd2);
        
        return 0;
}
