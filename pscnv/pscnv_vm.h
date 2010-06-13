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

#ifndef __PSCNV_VM_H__
#define __PSCNV_VM_H__

#include "pscnv_vram.h"
#include "pscnv_tree.h"

#define NV50_VM_SIZE		0x10000000000ULL
#define NV50_VM_PDE_COUNT	0x800
#define NV50_VM_SPTE_COUNT	0x20000
#define NV50_VM_LPTE_COUNT	0x2000

PSCNV_RB_HEAD(pscnv_vm_maptree, pscnv_vm_mapnode);

struct pscnv_vspace {
	int vid;
	struct drm_device *dev;
	struct mutex lock;
	int isbar;
	struct pscnv_vo *pt[NV50_VM_PDE_COUNT];
	struct list_head chan_list;
	struct pscnv_vm_maptree maps;
	struct drm_file *filp;
	int engines;
	struct kref ref;
};

struct pscnv_vm_mapnode {
	PSCNV_RB_ENTRY(pscnv_vm_mapnode) entry;
	struct pscnv_vspace *vspace;
	/* NULL means free */
	struct pscnv_vo *vo;
	uint64_t start;
	uint64_t size;
	uint64_t maxgap;
};

#define PSCNV_ENGINE_PGRAPH 0x00000001

extern int pscnv_vm_init(struct drm_device *);
extern int pscnv_vm_takedown(struct drm_device *);
extern struct pscnv_vspace *pscnv_vspace_new(struct drm_device *);
extern void pscnv_vspace_free(struct pscnv_vspace *);
extern int pscnv_vspace_map(struct pscnv_vspace *, struct pscnv_vo *, uint64_t start, uint64_t end, int back, struct pscnv_vm_mapnode **res);
extern int pscnv_vspace_unmap(struct pscnv_vspace *, uint64_t start);
extern int pscnv_vspace_unmap_node(struct pscnv_vm_mapnode *node);
extern int pscnv_vspace_map1(struct pscnv_vo *);
extern int pscnv_vspace_map3(struct pscnv_vo *);
extern void pscnv_vspace_ref_free(struct kref *ref);

extern void pscnv_vspace_cleanup(struct drm_device *dev, struct drm_file *file_priv);
extern int pscnv_mmap(struct file *filp, struct vm_area_struct *vma);

int pscnv_ioctl_vspace_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
int pscnv_ioctl_vspace_free(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
int pscnv_ioctl_vspace_map(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
int pscnv_ioctl_vspace_unmap(struct drm_device *dev, void *data,
						struct drm_file *file_priv);

/* needs vm_mutex held */
struct pscnv_vspace *pscnv_get_vspace(struct drm_device *dev, struct drm_file *file_priv, int vid);

#endif
