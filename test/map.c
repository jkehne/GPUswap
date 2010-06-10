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

	uint32_t size = 0x1000;
	uint64_t map_handle;
	uint32_t handle;
	ret = pscnv_gem_new(fd, 0xc071e, 0, 0x54, size, 0, &handle, &map_handle);
	if (ret) {
		printf("new: failed ret = %d\n", ret);
		return 1;
	}
	printf("new: handle %d map %llx\n", handle, map_handle);

	uint32_t *map = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_handle);
	printf ("Mapped at %p\n", map);

	for (i = 0; i < 0x400; i++)
		map[i] = 0xffffffff;
	for (i = 0; i < 0x400; i++)
		printf ("%x: %08x\n", i*4, map[i]);


        close (fd);
        close (fd2);
        
        return 0;
}
