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

#ifndef __PSCNV_DRM_H__
#define __PSCNV_DRM_H__

#define PSCNV_DRM_HEADER_PATCHLEVEL 1

#define PSCNV_GETPARAM_PCI_VENDOR      3
#define PSCNV_GETPARAM_PCI_DEVICE      4
#define PSCNV_GETPARAM_BUS_TYPE        5
#define PSCNV_GETPARAM_CHIPSET_ID      11
#define PSCNV_GETPARAM_GRAPH_UNITS     13
#define PSCNV_GETPARAM_PTIMER_TIME     14
#define PSCNV_GETPARAM_PFB_CONFIG      15
#define PSCNV_GETPARAM_VRAM_SIZE       16
struct drm_pscnv_getparam {
	uint64_t param;
	uint64_t value;
};

enum pscnv_bus_type {
	NV_AGP     = 0,
	NV_PCI     = 1,
	NV_PCIE    = 2,
};

#define DRM_PSCNV_GETPARAM           0x00

#endif /* __PSCNV_DRM_H__ */
