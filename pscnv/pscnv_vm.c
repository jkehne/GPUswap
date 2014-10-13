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
#include "pscnv_mem.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"
#include "pscnv_dma.h"


static int pscnv_vspace_bind (struct pscnv_vspace *vs, int fake) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	unsigned long flags;
	int i;
	BUG_ON(vs->vid);
	spin_lock_irqsave(&dev_priv->vm->vs_lock, flags);
	switch(fake) {
	case PSCNV_DMA_VSPACE:
		if (dev_priv->vm->vspaces[fake]) {
			NV_ERROR(vs->dev, "VM: vspace %d already allocated\n", fake);
			return -ENOSPC;
		}
		vs->vid = fake;
		dev_priv->vm->vspaces[fake] = vs;
		spin_unlock_irqrestore(&dev_priv->vm->vs_lock, flags);
		return 0;
	case 0:
		for (i = 1; i < 128; i++) {
			if (!dev_priv->vm->vspaces[i]) {
				vs->vid = i;
				dev_priv->vm->vspaces[i] = vs;
				spin_unlock_irqrestore(&dev_priv->vm->vs_lock, flags);
				return 0;
			}
		}
		spin_unlock_irqrestore(&dev_priv->vm->vs_lock, flags);
		NV_ERROR(vs->dev, "VM: Out of vspaces\n");
		return -ENOSPC;
	default:
		vs->vid = -fake;
		BUG_ON(dev_priv->vm->fake_vspaces[fake]);
		dev_priv->vm->fake_vspaces[fake] = vs;
		spin_unlock_irqrestore(&dev_priv->vm->vs_lock, flags);
		return 0;
	}
}

static void pscnv_vspace_unbind (struct pscnv_vspace *vs) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	unsigned long flags;
	spin_lock_irqsave(&dev_priv->vm->vs_lock, flags);
	if (vs->vid < 0) {
		BUG_ON(dev_priv->vm->fake_vspaces[-vs->vid] != vs);
		dev_priv->vm->fake_vspaces[-vs->vid] = 0;
	} else {
		BUG_ON(dev_priv->vm->vspaces[vs->vid] != vs);
		dev_priv->vm->vspaces[vs->vid] = 0;
	}
	vs->vid = 0;
	spin_unlock_irqrestore(&dev_priv->vm->vs_lock, flags);
}

struct pscnv_vspace *
pscnv_vspace_new (struct drm_device *dev, uint64_t size, uint32_t flags, int fake) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *res = kzalloc(sizeof *res, GFP_KERNEL);
	if (!res) {
		NV_ERROR(dev, "VM: Couldn't alloc vspace\n");
		return 0;
	}
	res->dev = dev;
	res->size = size;
	res->flags = flags;
	kref_init(&res->ref);
	mutex_init(&res->lock);
	if (pscnv_vspace_bind(res, fake)) {
		kfree(res);
		return 0;
	}
	NV_INFO(dev, "VM: Allocating vspace %d\n", res->vid);
	if (dev_priv->vm->do_vspace_new(res)) {
		pscnv_vspace_unbind(res);
		kfree(res);
		return 0;
	}
	return res;
}

static void
pscnv_vspace_free_unmap(struct pscnv_mm_node *node) {
	struct pscnv_bo *bo = node->bo;
	pscnv_mm_free(node);
	pscnv_bo_unref(bo);
}

void pscnv_vspace_ref_free(struct kref *ref) {
	struct pscnv_vspace *vs = container_of(ref, struct pscnv_vspace, ref);
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	NV_INFO(vs->dev, "VM: Freeing vspace %d\n", vs->vid);
	if (vs->vid < 0)
		pscnv_mm_takedown(vs->mm, pscnv_mm_free);
	else
		pscnv_mm_takedown(vs->mm, pscnv_vspace_free_unmap);
	dev_priv->vm->do_vspace_free(vs);
	pscnv_vspace_unbind(vs);
	kfree(vs);
}

static int
pscnv_vspace_unmap_node_unlocked(struct pscnv_mm_node *node) {
	struct pscnv_vspace *vs = node->vspace;
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	struct pscnv_bo *bo = node->bo;
	if (pscnv_vm_debug >= 1) {
		NV_INFO(vs->dev, "VM: vspace %d: Unmapping range %llx-%llx.\n", vs->vid, node->start, node->start + node->size);
	}
	dev_priv->vm->do_unmap(vs, node->start, node->size);

	pscnv_mm_free(node);
	
	if (vs->vid >= 0) {
		pscnv_bo_unref(bo);
	}
	
	return 0;
}

int
pscnv_vspace_map(struct pscnv_vspace *vs, struct pscnv_bo *bo,
		uint64_t start, uint64_t end, int back,
		struct pscnv_mm_node **res)
{
	struct pscnv_mm_node *node;
	int ret;
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	
	if (vs->vid >= 0) {
		pscnv_bo_ref(bo);
	}
	
	mutex_lock(&vs->lock);
	ret = dev_priv->vm->place_map(vs, bo, start, end, back, &node);
	if (ret) {
		mutex_unlock(&vs->lock);
		NV_INFO(vs->dev, "VM: vspace %d: Mapping BO %x/%d:"
			" place_map failed\n", vs->vid, bo->cookie, bo->serial);
		if (vs->vid >= 0) {
			pscnv_bo_unref(bo);
		}
		return ret;
	}
	node->bo = bo;
	node->vspace = vs;
	if (pscnv_vm_debug >= 1)
		NV_INFO(vs->dev, "VM: vspace %d: Mapping BO %x/%d at %llx-%llx.\n", vs->vid, bo->cookie, bo->serial, node->start,
				node->start + node->size);
	ret = dev_priv->vm->do_map(vs, bo, node->start);
	if (ret) {
		NV_ERROR(vs->dev, "VM: vspace %d: Mapping BO %x/%d at %llx-%llx. FAILED \n", vs->vid, bo->cookie, bo->serial, node->start,
				node->start + node->size);
		pscnv_vspace_unmap_node_unlocked(node); // includes unref(bo)
	}
	
	if (vs->vid >= 0 && vs->vid != 126 && !bo->primary_node) {
		bo->primary_node = node;
	}
	
	*res = node;
	mutex_unlock(&vs->lock);
	return ret;
}

int
pscnv_vspace_map_chunk(struct pscnv_vspace *vs, struct pscnv_chunk *cnk,
		uint64_t start, uint64_t end, int back,
		struct pscnv_mm_node **res)
{
	struct pscnv_mm_node *node;
	struct pscnv_bo *bo = cnk->bo;
	int ret;
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	
	if (vs->vid >= 0) {
		pscnv_bo_ref(bo);
	}
	
	mutex_lock(&vs->lock);
	ret = dev_priv->vm->place_map_chunk(vs, cnk, start, end, back, &node);
	if (ret) {
		mutex_unlock(&vs->lock);
		NV_INFO(vs->dev, "VM: vspace %d: Mapping Chunk %08x/%d-%u:"
			" place_map failed\n", vs->vid, bo->cookie, bo->serial,
			cnk->idx);
		if (vs->vid >= 0) {
			pscnv_bo_unref(bo);
		}
		return ret;
	}
	node->bo = bo;
	node->vspace = vs;
	if (pscnv_vm_debug >= 1) {
		NV_INFO(vs->dev, "VM: vspace %d: Mapping Chunk %08x/%d-%u at "
				 "%llx-%llx.\n", vs->vid, bo->cookie, bo->serial,
				cnk->idx, node->start, node->start + node->size);
	}
	
	ret = dev_priv->vm->do_map_chunk(vs, cnk, node->start);
	if (ret) {
		NV_ERROR(vs->dev, "VM: vspace %d: Mapping Chunk %08x/%d-%u at "
				 "%llx-%llx. FAILED \n", vs->vid, bo->cookie,
				bo->serial, cnk->idx, node->start,
				node->start + node->size);
		pscnv_vspace_unmap_node_unlocked(node); // includes unref(bo)
	}
	
	*res = node;
	mutex_unlock(&vs->lock);
	return ret;
}

int
pscnv_vspace_unmap_node(struct pscnv_mm_node *node) {
	struct pscnv_vspace *vs = node->vspace;
	int ret;
	mutex_lock(&vs->lock);
	ret = pscnv_vspace_unmap_node_unlocked(node);
	mutex_unlock(&vs->lock);
	return ret;
}

int
pscnv_vspace_unmap(struct pscnv_vspace *vs, uint64_t start) {
	int ret;
	mutex_lock(&vs->lock);
	ret = pscnv_vspace_unmap_node_unlocked(pscnv_mm_find_node(vs->mm, start));
	mutex_unlock(&vs->lock);
	return ret;
}

struct pscnv_bo *
pscnv_vspace_vm_addr_lookup(struct pscnv_vspace *vs, uint64_t addr)
{
	struct pscnv_mm_node *mm_node;
	
	mutex_lock(&vs->lock);
	mm_node = pscnv_mm_find_node(vs->mm, addr);
	mutex_unlock(&vs->lock);
	
	if (mm_node) {
		return mm_node->bo;
	}
	
	return NULL;
}
