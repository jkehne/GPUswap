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
#include <stdio.h>
#include "nouveau_drm.h"


/** Open the first DRM device we can find, searching up to 16 device nodes */
int drm_open_any(void)
{
	char name[20];
	int i, fd;

	for (i = 0; i < 16; i++) {
		sprintf(name, "/dev/fbs/drm%d", i);
		fd = open(name, O_RDWR);
		if (fd != -1) {
			return fd;
		}
	}
	printf("Failed to open drm\n");
	return -1;
}



int DoTest(int fd)
{
	struct drm_nouveau_channel_alloc nvchan;
	struct drm_nouveau_channel_free cfree;
	int ret;
	
	/* for 2D */
	nvchan.fb_ctxdma_handle = 0xD8000001;
	nvchan.tt_ctxdma_handle = 0xD8000002;
	nvchan.channel = -1;
	ret = ioctl(fd, DRM_IOCTL_NOUVEAU_CHANNEL_ALLOC, &nvchan);
	if (ret==0) {
		printf(" creat channel id = %d\n", nvchan.channel);
	} else {
		printf("failed create ret = %d\n", ret);
		return ret;
	}

	cfree.channel = nvchan.channel;
        ret = ioctl(fd, DRM_IOCTL_NOUVEAU_CHANNEL_FREE, &cfree);
        if (ret==0) {
                printf(" free channel id = %d\n", cfree.channel);
        } else {
                printf("failed to free ret = %d\n", ret);
        }
	return ret;
}


int
main()
{
	int fd;
	int result;
	
        fd = drm_open_any();

	if (fd == -1)
		return fd;
        result = DoTest(fd);
        
        close (fd);
        
        return result;
}
