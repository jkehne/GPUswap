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
#include <stdio.h>
#include "pscnv_drm.h"

int DoTest(int fd)
{
	struct drm_pscnv_getparam get_param;
	int ret;
	int i, j;
	uint64_t tmp[7];
	char param_name[7][15] = {"chipset_id", "vendor", "device", "bus type",
					"graph units", "ptimer time",
					"vram size"};

	tmp[0] = PSCNV_GETPARAM_CHIPSET_ID;
	tmp[1] = PSCNV_GETPARAM_PCI_VENDOR;
	tmp[2] = PSCNV_GETPARAM_PCI_DEVICE;
	tmp[3] = PSCNV_GETPARAM_BUS_TYPE;
	tmp[4] = PSCNV_GETPARAM_GRAPH_UNITS;
	tmp[5] = PSCNV_GETPARAM_PTIMER_TIME;
	tmp[6] = PSCNV_GETPARAM_VRAM_SIZE;

	
	for (i = 0; i < 7; i++) {
		get_param.param = tmp[i];
		ret = drmCommandWriteRead(fd, DRM_PSCNV_GETPARAM, &get_param, sizeof(get_param));
		if (ret==0) {
			printf("%s : 0x%llx\n", param_name[i], get_param.value);
		} else {
			printf("%s : failed ret = %d\n", param_name[i], ret);
		}
	}
	return ret;
}


int
main()
{
	int fd;
	int result;
	
        fd = drmOpen("pscnv", 0);

	if (fd == -1)
		return fd;
        result = DoTest(fd);
        
        close (fd);
        
        return result;
}
