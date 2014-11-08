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

#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_pm.h"
#include "pscnv_mem.h"
#include "pscnv_client.h"

#define NVC0_MEM_CTRLR_COUNT                                         0x00121c74
#define NVC0_MEM_CTRLR_RAM_AMOUNT                                    0x0010f20c

int nvc0_vram_alloc(struct pscnv_bo *bo);
int nvc0_sysram_tiling_ok(struct pscnv_bo *bo);

int
nvc0_vram_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;
	uint32_t ctrlr_num, ctrlr_amt;
	uint64_t vram_limit;

	dev_priv->vram_type = nouveau_mem_vbios_type(dev);

	ctrlr_num = nv_rd32(dev, NVC0_MEM_CTRLR_COUNT);
	ctrlr_amt = nv_rd32(dev, NVC0_MEM_CTRLR_RAM_AMOUNT);

	dev_priv->vram_size = ctrlr_num * (ctrlr_amt << 20);

	if (!dev_priv->vram_size) {
		NV_ERROR(dev, "No VRAM detected, aborting.\n");
		return -ENODEV;
	}

	NV_INFO(dev, "VRAM: size 0x%llx, %d controllers\n",
			dev_priv->vram_size, ctrlr_num);

	//limit VRAM if requested
	if (pscnv_vram_limit < 0) {
		NV_ERROR(dev, "Invalid VRAM limit, ignoring\n");
	} else {
		vram_limit = pscnv_vram_limit << 20;
		if (vram_limit && (dev_priv->vram_size > vram_limit)) {
			dev_priv->vram_size = vram_limit;
			/* reserve 8 MB for driver */
			dev_priv->vram_limit = vram_limit - (8 << 20);
			NV_INFO(dev, "Limiting VRAM to 0x%llx (%u MiB) as requested\n", dev_priv->vram_size, pscnv_vram_limit);
		}
	}

	ret = pscnv_mm_init(dev, "VRAM", 0x40000, dev_priv->vram_size - 0x20000, 0x1000, 0x20000, 0x1000, &dev_priv->vram_mm);

	if (ret) {
		WARN_ON(1);
		return ret;
	}

	return 0;
}

