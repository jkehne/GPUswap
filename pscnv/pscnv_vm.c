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

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "pscnv_vram.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"
#include "nv50_vm.h"

#undef PSCNV_RB_AUGMENT

static void PSCNV_RB_AUGMENT(struct pscnv_vm_mapnode *node) {
	uint64_t maxgap = 0;
	struct pscnv_vm_mapnode *left = PSCNV_RB_LEFT(node, entry);
	struct pscnv_vm_mapnode *right = PSCNV_RB_RIGHT(node, entry);
	if (!node->vo)
		maxgap = node->size;
	if (left && left->maxgap > maxgap)
		maxgap = left->maxgap;
	if (right && right->maxgap > maxgap)
		maxgap = right->maxgap;
	node->maxgap = maxgap;
}

static int mapcmp(struct pscnv_vm_mapnode *a, struct pscnv_vm_mapnode *b) {
	if (a->start < b->start)
		return -1;
	else if (a->start > b->start)
		return 1;
	return 0;
}

PSCNV_RB_GENERATE_STATIC(pscnv_vm_maptree, pscnv_vm_mapnode, entry, mapcmp)

static int
pscnv_vspace_do_unmap (struct pscnv_vspace *vs, uint64_t offset, uint64_t length) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	int ret;
	while (length) {
		uint32_t pgnum = offset / 0x1000;
		uint32_t pdenum = pgnum / NV50_VM_SPTE_COUNT;
		uint32_t ptenum = pgnum % NV50_VM_SPTE_COUNT;
		if (vs->pt[pdenum]) {
			nv_wv32(vs->pt[pdenum], ptenum * 8, 0);
		}
		offset += 0x1000;
		length -= 0x1000;
	}
	if (vs->isbar) {
		return nv50_vm_flush(vs->dev, 6);
	} else {
		int i;
		for (i = 0; i < PSCNV_ENGINES_NUM; i++) {
			struct pscnv_engine *eng = dev_priv->engines[i];
			if (vs->engref[i])
				if ((ret = eng->tlb_flush(eng, vs)))
					return ret;
		}
	}
	return 0;
}

static int
pscnv_vspace_fill_pd_slot (struct pscnv_vspace *vs, uint32_t pdenum) {
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	struct list_head *pos;
	int i;
	uint32_t chan_pd;
	vs->pt[pdenum] = pscnv_vram_alloc(vs->dev, NV50_VM_SPTE_COUNT * 8, PSCNV_VO_CONTIG, 0, 0xa9e7ab1e);
	if (!vs->pt[pdenum]) {
		return -ENOMEM;
	}

	if (!vs->isbar)
		pscnv_vspace_map3(vs->pt[pdenum]);

	for (i = 0; i < NV50_VM_SPTE_COUNT; i++)
		nv_wv32(vs->pt[pdenum], i * 8, 0);

	if (dev_priv->chipset == 0x50)
		chan_pd = NV50_CHAN_PD;
	else
		chan_pd = NV84_CHAN_PD;

	list_for_each(pos, &vs->chan_list) {
		struct pscnv_chan *ch = list_entry(pos, struct pscnv_chan, vspace_list);
		uint64_t pde = vs->pt[pdenum]->start | 3;
		nv_wv32(ch->vo, chan_pd + pdenum * 8 + 4, pde >> 32);
		nv_wv32(ch->vo, chan_pd + pdenum * 8, pde);
	}
	return 0;
}

static int
pscnv_vspace_do_map (struct pscnv_vspace *vs, struct pscnv_vo *vo, uint64_t offset) {
	struct list_head *pos;
	int ret;
	list_for_each(pos, &vo->regions) {
		/* XXX: beef up to use contig blocks */
		struct pscnv_vram_region *reg = list_entry(pos, struct pscnv_vram_region, local_list);
		uint64_t roff;
		for (roff = 0; roff < reg->size; roff += 0x1000, offset += 0x1000) {
			uint32_t pgnum = offset / 0x1000;
			uint32_t pdenum = pgnum / NV50_VM_SPTE_COUNT;
			uint32_t ptenum = pgnum % NV50_VM_SPTE_COUNT;
			uint64_t pte = reg->start + roff;
			pte |= (uint64_t)vo->tile_flags << 40;
			pte |= 1; /* present */
			if (!vs->pt[pdenum])
				if ((ret = pscnv_vspace_fill_pd_slot (vs, pdenum))) {
					pscnv_vspace_do_unmap (vs, offset, vo->size);
					return ret;
				}
			nv_wv32(vs->pt[pdenum], ptenum * 8 + 4, pte >> 32);
			nv_wv32(vs->pt[pdenum], ptenum * 8, pte);
		}
	}
	return 0;
}

struct pscnv_vspace *
pscnv_vspace_new (struct drm_device *dev) {
	struct pscnv_vspace *res = kzalloc(sizeof *res, GFP_KERNEL);
	struct pscnv_vm_mapnode *fmap;
	if (!res)
		return 0;
	res->dev = dev;
	kref_init(&res->ref);
	mutex_init(&res->lock);
	INIT_LIST_HEAD(&res->chan_list);
	PSCNV_RB_INIT(&res->maps);
	fmap = kzalloc(sizeof *fmap, GFP_KERNEL);
	if (!fmap) {
		kfree(res);
		return 0;
	}
	fmap->vspace = res;
	fmap->start = 0;
	fmap->size = 1ULL << 40;
	fmap->maxgap = fmap->size;
	PSCNV_RB_INSERT(pscnv_vm_maptree, &res->maps, fmap);
	return res;
}

void
pscnv_vspace_free(struct pscnv_vspace *vs) {
	int i;
	struct pscnv_vm_mapnode *node;
	while ((node = PSCNV_RB_ROOT(&vs->maps))) {
		if (node->vo && !vs->isbar) {
			drm_gem_object_unreference_unlocked(node->vo->gem);
		}
		PSCNV_RB_REMOVE(pscnv_vm_maptree, &vs->maps, node);
		kfree(node);
	}
	for (i = 0; i < NV50_VM_PDE_COUNT; i++) {
		if (vs->pt[i]) {
			pscnv_vram_free(vs->pt[i]);
		}
	}
	kfree(vs);
}

void pscnv_vspace_ref_free(struct kref *ref) {
	struct pscnv_vspace *vs = container_of(ref, struct pscnv_vspace, ref);
	int vid = vs->vid;
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;

	NV_INFO(vs->dev, "Freeing VSPACE %d\n", vid);

	pscnv_vspace_free(vs);

	dev_priv->vspaces[vid] = 0;
}

int
pscnv_vm_init(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *barvm = pscnv_vspace_new (dev);
	struct pscnv_chan *barch;
	int bar1dma, bar3dma;
	if (!barvm)
		return -ENOMEM;
	barvm->isbar = 1;
	barch = pscnv_chan_new (barvm);
	if (!barch)
		return -ENOMEM;
	nv_wr32(dev, 0x1704, 0x40000000 | barch->vo->start >> 12);
	bar1dma = pscnv_chan_dmaobj_new(barch, 0x7fc00000, 0, dev_priv->fb_size);
	bar3dma = pscnv_chan_dmaobj_new(barch, 0x7fc00000, dev_priv->fb_size, dev_priv->ramin_size);
	nv_wr32(dev, 0x1708, 0x80000000 | bar1dma >> 4);
	nv_wr32(dev, 0x170c, 0x80000000 | bar3dma >> 4);
	dev_priv->barvm = barvm;
	dev_priv->barch = barch;
	mutex_init(&dev_priv->vm_mutex);
	pscnv_vspace_map3(barch->vo);
	pscnv_vspace_map3(barvm->pt[0]);
	return 0;
}

int
pscnv_vm_takedown(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *vs = dev_priv->barvm;
	struct pscnv_chan *ch = dev_priv->barch;
	/* XXX: write me. */
	dev_priv->barvm = 0;
	dev_priv->barch = 0;
	nv_wr32(dev, 0x1708, 0);
	nv_wr32(dev, 0x170c, 0);
	nv_wr32(dev, 0x1710, 0);
	nv_wr32(dev, 0x1704, 0);
	pscnv_chan_free(ch);
	pscnv_vspace_free(vs);
	return 0;
}

static struct pscnv_vm_mapnode *
pscnv_vspace_map_int(struct pscnv_vspace *vs, struct pscnv_vo *vo,
		uint64_t start, uint64_t end, int back,
		struct pscnv_vm_mapnode *node)
{
	struct pscnv_vm_mapnode *left, *right, *res;
	int lok, rok;
	uint64_t mstart, mend;
	left = PSCNV_RB_LEFT(node, entry);
	right = PSCNV_RB_RIGHT(node, entry);
	lok = left && left->maxgap >= vo->size && node->start > start;
	rok = right && right->maxgap >= vo->size && node->start + node->size  < end;
	if (pscnv_vm_debug >= 2)
		NV_INFO (vs->dev, "VM map: %llx %llx %llx %llx %llx %llx %llx %llx %llx %d %d\n", node->start, node->size, node->maxgap,
				left?left->start:0, left?left->size:0, left?left->maxgap:0,
				right?right->start:0, right?right->size:0, right?right->maxgap:0, lok, rok);
	if (!back && lok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, left);
		if (res)
			return res;
	}
	if (back && rok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, right);
		if (res)
			return res;
	}
	mstart = node->start;
	if (mstart < start)
		mstart = start;
	mend = node->start + node->size;
	if (mend > end)
		mend = end;
	if (mstart + vo->size <= mend && !node->vo) {
		if (back)
			mstart = mend - vo->size;
		mend = mstart + vo->size;
		if (node->start + node->size != mend) {
			struct pscnv_vm_mapnode *split = kzalloc(sizeof *split, GFP_KERNEL);
			if (!split)
				return 0;
			split->start = mend;
			split->size = node->start + node->size - mend;
			split->vspace = vs;
			node->size = mend - node->start;
			split->maxgap = split->size;
			PSCNV_RB_INSERT(pscnv_vm_maptree, &vs->maps, split);
		}
		if (node->start != mstart) {
			struct pscnv_vm_mapnode *split = kzalloc(sizeof *split, GFP_KERNEL);
			if (!split)
				return 0;
			split->start = node->start;
			split->size = mstart - node->start;
			split->vspace = vs;
			node->start = mstart;
			node->size = mend - node->start;
			split->maxgap = split->size;
			PSCNV_RB_INSERT(pscnv_vm_maptree, &vs->maps, split);
		}
		node->vo = vo;
		PSCNV_RB_AUGMENT(node);
		return node;
	}
	if (back && lok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, left);
		if (res)
			return res;
	}
	if (!back && rok) {
		res = pscnv_vspace_map_int(vs, vo, start, end, back, right);
		if (res)
			return res;
	}
	return 0;
}

int
pscnv_vspace_map(struct pscnv_vspace *vs, struct pscnv_vo *vo,
		uint64_t start, uint64_t end, int back,
		struct pscnv_vm_mapnode **res)
{
	struct pscnv_vm_mapnode *node;
	start += 0xfff;
	start &= ~0xfffull;
	end &= ~0xfffull;
	if (end > (1ull << 40))
		end = 1ull << 40;
	if (start >= end)
		return -EINVAL;
	mutex_lock(&vs->lock);
	node = pscnv_vspace_map_int(vs, vo, start, end, back, PSCNV_RB_ROOT(&vs->maps));
	if (!node) {
		mutex_unlock(&vs->lock);
		return -ENOMEM;
	}
	if (pscnv_vm_debug >= 1)
		NV_INFO(vs->dev, "Mapping VO %x/%d at %llx-%llx.\n", vo->cookie, vo->serial, node->start,
				node->start + node->size);
	pscnv_vspace_do_map(vs, vo, node->start);
	*res = node;
	mutex_unlock(&vs->lock);
	return 0;
}

static int
pscnv_vspace_unmap_node_unlocked(struct pscnv_vm_mapnode *node) {
	if (pscnv_vm_debug >= 1) {
		NV_INFO(node->vspace->dev, "Unmapping range %llx-%llx.\n", node->start, node->start + node->size);
	}
	pscnv_vspace_do_unmap(node->vspace, node->start, node->size);
	if (!node->vspace->isbar) {
		drm_gem_object_unreference(node->vo->gem);
	}
	node->vo = 0;
	node->maxgap = node->size;
	PSCNV_RB_AUGMENT(node);
	/* XXX: try merge */
	return 0;
}

int
pscnv_vspace_unmap_node(struct pscnv_vm_mapnode *node) {
	struct pscnv_vspace *vs = node->vspace;
	int ret;
	mutex_lock(&vs->lock);
	ret = pscnv_vspace_unmap_node_unlocked(node);
	mutex_unlock(&vs->lock);
	return ret;
}

int
pscnv_vspace_unmap(struct pscnv_vspace *vs, uint64_t start) {
	struct pscnv_vm_mapnode *node;
	int ret;
	mutex_lock(&vs->lock);
	node = PSCNV_RB_ROOT(&vs->maps);
	while (node) {
		if (node->start == start && node->vo) {
			ret = pscnv_vspace_unmap_node_unlocked(node);
			mutex_unlock(&vs->lock);
			return ret;
		}
		if (start < node->start)
			node = PSCNV_RB_LEFT(node, entry);
		else
			node = PSCNV_RB_RIGHT(node, entry);
	}
	mutex_unlock(&vs->lock);
	return -ENOENT;
}

int pscnv_vspace_map1(struct pscnv_vo *vo) {
	struct drm_nouveau_private *dev_priv = vo->dev->dev_private;
	if (vo->map1)
		return 0;
	if (!dev_priv->barvm)
		return -ENODEV;
	return pscnv_vspace_map(dev_priv->barvm, vo, 0, dev_priv->fb_size, 0, &vo->map1);
}

int pscnv_vspace_map3(struct pscnv_vo *vo) {
	struct drm_nouveau_private *dev_priv = vo->dev->dev_private;
	if (vo->map3)
		return 0;
	if (!dev_priv->barvm)
		return -ENODEV;
	return pscnv_vspace_map(dev_priv->barvm, vo, dev_priv->fb_size, dev_priv->fb_size + dev_priv->ramin_size, 0, &vo->map3);
}

static struct vm_operations_struct pscnv_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};	

int pscnv_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct pscnv_vo *vo;
	int ret;

	if (vma->vm_pgoff * PAGE_SIZE < (1ull << 31))
		return drm_mmap(filp, vma);

	if (vma->vm_pgoff * PAGE_SIZE < (1ull << 32))
		return pscnv_chan_mmap(filp, vma);

	obj = drm_gem_object_lookup(dev, priv, (vma->vm_pgoff * PAGE_SIZE) >> 32);
	if (!obj)
		return -ENOENT;
	vo = obj->driver_private;
	
	if (vma->vm_end - vma->vm_start > vo->size) {
		drm_gem_object_unreference_unlocked(obj);
		return -EINVAL;
	}
	if ((ret = pscnv_vspace_map1(vo))) {
		drm_gem_object_unreference_unlocked(obj);
		return ret;
	}

	vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTEXPAND;
	vma->vm_ops = &pscnv_vm_ops;
	vma->vm_private_data = obj;
	vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));

	vma->vm_file = filp;

	return remap_pfn_range(vma, vma->vm_start, 
			(dev_priv->fb_phys + vo->map1->start) >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, PAGE_SHARED);
}

/* needs vm_mutex held */
struct pscnv_vspace *
pscnv_get_vspace(struct drm_device *dev, struct drm_file *file_priv, int vid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (vid < 128 && vid >= 0 && dev_priv->vspaces[vid] && dev_priv->vspaces[vid]->filp == file_priv) {
		return dev_priv->vspaces[vid];
	}
	return 0;
}

int pscnv_ioctl_vspace_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_req *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int vid = -1;
	int i;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	for (i = 0; i < 128; i++)
		if (!dev_priv->vspaces[i]) {
			vid = i;
			break;
		}

	if (vid == -1) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOSPC;
	}

	dev_priv->vspaces[vid] = pscnv_vspace_new(dev);
	if (!dev_priv->vspaces[i]) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOMEM;
	}

	dev_priv->vspaces[vid]->filp = file_priv;
	dev_priv->vspaces[vid]->vid = vid;
	
	req->vid = vid;

	NV_INFO(dev, "Allocating VSPACE %d\n", vid);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

int pscnv_ioctl_vspace_free(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_req *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int vid = req->vid;
	struct pscnv_vspace *vs;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);
	vs = pscnv_get_vspace(dev, file_priv, vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	vs->filp = 0;
	kref_put(&vs->ref, pscnv_vspace_ref_free);

	mutex_unlock (&dev_priv->vm_mutex);
	return 0;
}

int pscnv_ioctl_vspace_map(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_map *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *vs;
	struct drm_gem_object *obj;
	struct pscnv_vo *vo;
	struct pscnv_vm_mapnode *map;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	vs = pscnv_get_vspace(dev, file_priv, req->vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	obj = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!obj) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -EBADF;
	}

	vo = obj->driver_private;

	ret = pscnv_vspace_map(vs, vo, req->start, req->end, req->back, &map);
	if (map)
		req->offset = map->start;

	mutex_unlock (&dev_priv->vm_mutex);
	return ret;
}

int pscnv_ioctl_vspace_unmap(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_vspace_unmap *req = data;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *vs;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	mutex_lock (&dev_priv->vm_mutex);

	vs = pscnv_get_vspace(dev, file_priv, req->vid);
	if (!vs) {
		mutex_unlock (&dev_priv->vm_mutex);
		return -ENOENT;
	}

	ret = pscnv_vspace_unmap(vs, req->offset);

	mutex_unlock (&dev_priv->vm_mutex);
	return ret;
}

void pscnv_vspace_cleanup(struct drm_device *dev, struct drm_file *file_priv) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int vid;
	struct pscnv_vspace *vs;

	mutex_lock (&dev_priv->vm_mutex);
	for (vid = 0; vid < 128; vid++) {
		vs = pscnv_get_vspace(dev, file_priv, vid);
		if (!vs)
			continue;
		vs->filp = 0;
		kref_put(&vs->ref, pscnv_vspace_ref_free);
	}
	mutex_unlock (&dev_priv->vm_mutex);
}

/* VM trap handling on NV50 is some kind of a fucking joke.
 *
 * So, there's this little bugger called MMU, which is in PFB area near
 * 0x100c80 and contains registers to flush the TLB caches, and to report
 * VM traps.
 *
 * And you have several units making use of that MMU. The known ones atm
 * include PGRAPH, PFIFO, the BARs, and the PEEPHOLE. Each of these has its
 * own TLBs. And most of them have several subunits, each having a separate
 * MMU access path.
 *
 * Now, if you use an address that is bad in some way, the MMU responds "NO
 * PAGE!!!11!1". And stores the relevant address + unit + channel into
 * 0x100c90 area, where you can read it. However, it does NOT report an
 * interrupt - this is done by the faulting unit.
 *
 * Now, if you get several page faults at once, which is not that uncommon
 * if you fuck up something in your code, all but the first trap is lost.
 * The unit reporting the trap may or may not also store the address on its
 * own.
 *
 * So we report the trap in two pieces. First we go through all the possible
 * faulters and report their status, which may range anywhere from full access
 * info [like TPDMA] to just "oh! a trap!" [like VFETCH]. Then we ask the MMU
 * for whatever trap it remembers. Then the user can look at dmesg and maybe
 * match them using the MMU status field. Which we should decode someday, but
 * meh for now.
 *
 * As for the Holy Grail of Demand Paging - hah. Who the hell knows. Given the
 * fucked up reporting, the only hope lies in getting all individual units to
 * cooperate. BAR accesses quite obviously cannot be demand paged [not a big
 * problem - that's what host page tables are for]. PFIFO accesses all seem
 * restartable just fine. As for PGRAPH... some, like TPDMA, are already dead
 * when they happen, but maybe there's a DEBUG bit somewhere that changes it.
 * Some others, like M2MF, hang on fault, and are therefore promising. But
 * this requires shitloads of RE repeated for every unit. Have fun.
 *
 */

struct pscnv_enumval {
	int value;
	char *name;
	void *data;
};

static struct pscnv_enumval vm_trap_reasons[] = {
	{ 0, "PT_NOT_PRESENT", 0},
	{ 1, "PT_TOO_SHORT", 0 },
	{ 2, "PAGE_NOT_PRESENT", 0 },
	/* 3 is magic flag 0x40 set in PTE */
	{ 4, "PAGE_READ_ONLY", 0 },
	/* 5 never seen */
	{ 6, "NULL_DMAOBJ", 0 },
	/* 7-0xa never seen */
	{ 0xb, "VRAM_LIMIT", 0 },
	/* 0xc-0xe never seen */
	{ 0xf, "DMAOBJ_LIMIT", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_dispatch_subsubunits[] = {
	{ 0, "GRCTX", 0 },
	{ 1, "NOTIFY", 0 },
	{ 2, "QUERY", 0 },
	{ 3, "COND", 0 },
	{ 4, "M2M_IN", 0 },
	{ 5, "M2M_OUT", 0 },
	{ 6, "M2M_NOTIFY", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_ccache_subsubunits[] = {
	{ 0, "CB", 0 },
	{ 1, "TIC", 0 },
	{ 2, "TSC", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_tprop_subsubunits[] = {
	{ 0, "RT0", 0 },
	{ 1, "RT1", 0 },
	{ 2, "RT2", 0 },
	{ 3, "RT3", 0 },
	{ 4, "RT4", 0 },
	{ 5, "RT5", 0 },
	{ 6, "RT6", 0 },
	{ 7, "RT7", 0 },
	{ 8, "ZETA", 0 },
	{ 9, "LOCAL", 0 },
	{ 0xa, "GLOBAL", 0 },
	{ 0xb, "STACK", 0 },
	{ 0xc, "DST2D", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_pgraph_subunits[] = {
	{ 0, "STRMOUT", 0 },
	{ 3, "DISPATCH", vm_dispatch_subsubunits },
	{ 5, "CCACHE", vm_ccache_subsubunits },
	{ 7, "CLIPID", 0 },
	{ 9, "VFETCH", 0 },
	{ 0xa, "TEXTURE", 0 },
	{ 0xb, "TPROP", vm_tprop_subsubunits },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_pfifo_subsubunits[] = {
	{ 0, "PUSHBUF", 0 },
	{ 1, "SEMAPHORE", 0 },
	/* 3 seen. also on semaphore. but couldn't reproduce. */
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_pfifo_subunits[] = {
	/* curious. */
	{ 8, "FIFO", vm_pfifo_subsubunits },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_peephole_subunits[] = {
	/* even more curious. */
	{ 4, "WRITE", 0 },
	{ 8, "READ", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_bar_subsubunits[] = {
	{ 0, "FB", 0 },
	{ 1, "IN", 0 },
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_bar_subunits[] = {
	/* even more curious. */
	{ 4, "WRITE", vm_bar_subsubunits },
	{ 8, "READ", vm_bar_subsubunits },
	/* 0xa also seen. some kind of write. */
	{ 0, 0, 0 },
};

static struct pscnv_enumval vm_units[] = {
	{ 0, "PGRAPH", vm_pgraph_subunits },
	{ 1, "PVP", 0 },
	/* 2, 3 never seen */
	{ 4, "PEEPHOLE", vm_peephole_subunits },
	{ 5, "PFIFO", vm_pfifo_subunits },
	{ 6, "BAR", vm_bar_subunits },
	/* 7 never seen */
	{ 8, "PPPP", 0 },
	{ 9, "PBSP", 0 },
	{ 0xa, "PCRYPT", 0 },
	/* 0xb, 0xc never seen */
	{ 0xd, "PVUNK104", 0 },
	/* 0xe: UNK10a000??? */
	{ 0, 0, 0 },
};

static struct pscnv_enumval *pscnv_enum_find (struct pscnv_enumval *list, int val) {
	while (list->value != val && list->name)
		list++;
	if (list->name)
		return list;
	else
		return 0;
}

void pscnv_vm_trap(struct drm_device *dev) {
	/* XXX: go through existing channels and match the address */
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t trap[6];
	int i;
	uint32_t idx = nv_rd32(dev, 0x100c90);
	uint32_t s0, s1, s2, s3;
	char reason[50];
	char unit1[50];
	char unit2[50];
	char unit3[50];
	struct pscnv_enumval *ev;
	if (idx & 0x80000000) {
		idx &= 0xffffff;
		for (i = 0; i < 6; i++) {
			nv_wr32(dev, 0x100c90, idx | i << 24);
			trap[i] = nv_rd32(dev, 0x100c94);
		}
		if (dev_priv->chipset < 0xa3 || dev_priv->chipset >= 0xaa) {
			s0 = trap[0] & 0xf;
			s1 = (trap[0] >> 4) & 0xf;
			s2 = (trap[0] >> 8) & 0xf;
			s3 = (trap[0] >> 12) & 0xf;
		} else {
			s0 = trap[0] & 0xff;
			s1 = (trap[0] >> 8) & 0xff;
			s2 = (trap[0] >> 16) & 0xff;
			s3 = (trap[0] >> 24) & 0xff;
		}
		ev = pscnv_enum_find(vm_trap_reasons, s1);
		if (ev)
			snprintf(reason, sizeof(reason), "%s", ev->name);
		else
			snprintf(reason, sizeof(reason), "0x%x", s1);
		ev = pscnv_enum_find(vm_units, s0);
		if (ev)
			snprintf(unit1, sizeof(unit1), "%s", ev->name);
		else
			snprintf(unit1, sizeof(unit1), "0x%x", s0);
		if (ev && (ev = ev->data) && (ev = pscnv_enum_find(ev, s2)))
			snprintf(unit2, sizeof(unit2), "%s", ev->name);
		else
			snprintf(unit2, sizeof(unit2), "0x%x", s2);
		if (ev && (ev = ev->data) && (ev = pscnv_enum_find(ev, s3)))
			snprintf(unit3, sizeof(unit3), "%s", ev->name);
		else
			snprintf(unit3, sizeof(unit3), "0x%x", s3);
		NV_INFO(dev, "VM: Trapped %s at %02x%04x%04x channel %04x%04x on %s/%s/%s, reason %s\n",
				(trap[5]&0x100?"read":"write"),
				trap[5]&0xff, trap[4]&0xffff,
				trap[3]&0xffff, trap[2], trap[1], unit1, unit2, unit3, reason);
		nv_wr32(dev, 0x100c90, idx | 0x80000000);
	}
}
