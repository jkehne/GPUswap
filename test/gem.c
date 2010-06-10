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

int
main()
{
	int fd, fd2;
	int result;
	int ret;
	
        fd = drmOpen("pscnv", 0);
        fd2 = drmOpen("pscnv", 0);

	if (fd == -1 || fd2 == -1)
		return 1;

	uint64_t map_handle;
	uint32_t handle;
	uint32_t user[8];

	user[0] = 0xdeadbeef;
	user[1] = 0xcafebabe;

	ret = pscnv_gem_new(fd, 0xc071e, 0, 0x70, 0x1234, user, &handle, &map_handle);
	if (ret) {
		printf("new: failed ret = %d\n", ret);
		return 1;
	}
	printf("new: handle %d map %llx\n", handle, map_handle);

	uint32_t name;
	ret = pscnv_gem_flink(fd, handle, &name);
	if (ret) {
		printf("flink: failed ret = %d\n", ret);
		return 1;
	}
	printf ("flink: name %d\n", name);

	uint32_t handle2;
	uint64_t size;
	ret = pscnv_gem_open(fd2, name, &handle2, &size);
	if (ret) {
		printf("open: failed ret = %d\n", ret);
		return 1;
	}
	printf ("open: handle %d size %llx\n", handle2, size);

	ret = pscnv_gem_close(fd, handle);
	if (ret) {
		printf("close1: failed ret = %d\n", ret);
		return 1;
	}

	uint32_t cookie, flags, tile_flags;

	ret = pscnv_gem_info(fd2, handle2, &cookie, &flags, &tile_flags, &size, &map_handle, user);
	if (ret) {
		printf("info: failed ret = %d\n", ret);
		return 1;
	}
	printf("info: handle %d map %llx\n", handle2, map_handle);
	printf("info: cookie %x flags %x tf %x\n", cookie, flags, tile_flags);
	printf("info: size %llx user %x %x\n", size, user[0], user[1]);
        
	ret = pscnv_gem_close(fd2, handle2);
	if (ret) {
		printf("close2: failed ret = %d\n", ret);
		return 1;
	}

        close (fd);
        close (fd2);
        
        return 0;
}
