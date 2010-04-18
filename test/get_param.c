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
		sprintf(name, "/dev/dri/card%d", i);
		fd = open(name, O_RDWR);
		if (fd != -1)
			return fd;
	}
	abort();
}



int DoTest(int fd)
{
	struct drm_nouveau_getparam get_param;
	int ret;
	int tmp[11], i, j;
	char param_name[11][15];
	tmp[0] = NOUVEAU_GETPARAM_CHIPSET_ID;
	tmp[1] = NOUVEAU_GETPARAM_PCI_VENDOR;
	tmp[2] = NOUVEAU_GETPARAM_PCI_DEVICE;
	tmp[3] = NOUVEAU_GETPARAM_BUS_TYPE;
	tmp[4] = NOUVEAU_GETPARAM_FB_PHYSICAL;
	tmp[5] = NOUVEAU_GETPARAM_AGP_PHYSICAL;
	tmp[6] = NOUVEAU_GETPARAM_PCI_PHYSICAL;
	tmp[7] = NOUVEAU_GETPARAM_FB_SIZE;
	tmp[8] = NOUVEAU_GETPARAM_AGP_SIZE;
	tmp[9] = NOUVEAU_GETPARAM_VM_VRAM_BASE;
	tmp[10] = NOUVEAU_GETPARAM_GRAPH_UNITS;

	param_name[0] = "chipset_id";
	param_name[1] = "vender";
	param_name[2] = "device";
	param_name[3] = "bus type";
	param_name[4] = "fb addr";
	param_name[5] = "AGP addr";
	param_name[6] = "pci addr";
	param_name[7] = "fb size";
	param_name[8] = "agp size";
	param_name[9] = "vm vram base";
	param_name[10] = "graph units";
	
	for (i = 0; i < 11; i++) {
		get_param.param = tmp[i];
		ret = ioctl(fd, DRM_IOCTL_NOUVEAU_GETPARAM, &get_param);
		assert (ret==0);
		printf("%s : 0x%x\n", param_name[i], get_param.value);
	}
	return ret;
}


int
main()
{
	int fd;
	int result;
	
        fd = drm_open_any();

        result = DoTest(fd);
        
        close (fd);
        
        return result;
}
