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
	uint64_t param;		/* < */
	uint64_t value;		/* > */
};

enum pscnv_bus_type {
	NV_AGP     = 0,
	NV_PCI     = 1,
	NV_PCIE    = 2,
};

/* used for gem_new and gem_info */
struct drm_pscnv_gem_info {	/* n i */
	/* GEM handle used for identification */
	uint32_t handle;	/* > < */
	/* cookie: free-form 32-bit number displayed in debug info. */
	uint32_t cookie;	/* < > */
	/* misc flags, see below. */
	uint32_t flags;		/* < > */
	uint32_t tile_flags;	/* < > */
	uint64_t size;		/* < > */
	/* offset inside drm fd's vm space usable for mmapping */
	uint64_t map_handle;	/* > > */
	/* unused by kernel, can be used by userspace to store some info,
	 * like buffer format and tile_mode for DRI2 */
	uint32_t user[8];	/* < > */
};
#define PSCNV_GEM_CONTIG	0x00000001	/* needs to be contiguous in VRAM */

#define DRM_PSCNV_GETPARAM           0x00	/* get some information from the card */
#define DRM_PSCNV_GEM_NEW            0x20	/* create a new BO */
#define DRM_PSCNV_GEM_INFO           0x21	/* get info about a BO */
/* also uses generic GEM close, flink, open ioctls */

#endif /* __PSCNV_DRM_H__ */
