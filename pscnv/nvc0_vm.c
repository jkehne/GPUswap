/*
 * Copyright 2010 Christoph Bumiller.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include "pscnv_mem.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"
#include "nvc0_vm.h"

#define PSCNV_GEM_NOUSER 0x10 /* XXX */

static int nvc0_vm_map_kernel(struct pscnv_bo *bo);
static void nvc0_vm_takedown(struct drm_device *dev);

static char *
nvc0_tlb_flush_type_str(uint32_t type)
{
	switch (type) {
		case 0x1: return "PAGE_ALL";
		case 0x5: return "HUB_ONLY";
	}
	return "unknown type";
}

static int
nvc0_tlb_flush(struct pscnv_vspace *vs)
{
	struct drm_device *dev = vs->dev;
	uint32_t type;
	int ret = 0;
	unsigned long flags;

	BUG_ON(!nvc0_vs(vs)->pd);
	
	type = 0x1; /* PAGE_ALL */
	
	/* in original pscnv this was vs->vid == -3
	 * I think this is wrong. In nouveau, the same code (vm/nvc0.c) reads:
	 *
	 *   type = 0x00000001;
	 *   if (atomic_read(&vm->engref[NVDEV_SUBDEV_BAR]))
	 *        type |= 0x00000004;
	 *
	 * and NVDEV_SUBDEV_BAR is translated to "BAR1" in fifo/nvc0.c
	 * if it was BAR3, it should be NVDEV_SUBDEV_INSTMEM
	 *
	 * actually, I did not realize any difference however HUB_ONLY was set*/
	if (vs->vid == -1) {
		type |= 0x4; /* HUB_ONLY */
	}

	NV_DEBUG(dev, "nvc0_tlb_flush 0x%010llx\n", nvc0_vs(vs)->pd->start);

	if (!nv_wait_neq(dev, 0x100c80, 0x00ff0000, 0x00000000)) {
		NV_ERROR(dev, "TLB FLUSH TIMEOUT 0: vspace=%d 0x%08x %s\n",
				vs->vid, nv_rd32(dev, 0x100c80),
				nvc0_tlb_flush_type_str(type));
		ret = -EBUSY;
	}
	
	spin_lock_irqsave(&nvc0_vs(vs)->pd_lock, flags);

	nv_wr32(dev, 0x100cb8, nvc0_vs(vs)->pd->start >> 8);
	nv_wr32(dev, 0x100cbc, 0x80000000 | type);

	/* wait for flush to be queued?
	 * nv_wait does busy waiting on device timer, so it should be safe
	 * to disable irqs here */
	if (!nv_wait(dev, 0x100c80, 0x00008000, 0x00008000)) {
		spin_unlock_irqrestore(&nvc0_vs(vs)->pd_lock, flags);
		NV_ERROR(dev, "TLB FLUSH TIMEOUT 1: vspace=%d 0x%08x %s\n",
			 	vs->vid, nv_rd32(dev, 0x100c80),
			 	nvc0_tlb_flush_type_str(type));
		ret = -EBUSY;
	}
	
	spin_unlock_irqrestore(&nvc0_vs(vs)->pd_lock, flags);
	
	return ret;
}

static int
nvc0_vspace_fill_pde(struct pscnv_vspace *vs, struct nvc0_pgt *pgt)
{
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	const uint32_t size = NVC0_VM_SPTE_COUNT << (3 - pgt->limit);
	int i;
	uint32_t pde[2];
	unsigned long flags;

	pgt->bo[1] = pscnv_mem_alloc(vs->dev, size, PSCNV_GEM_CONTIG, 0, 0x59, NULL);
	if (!pgt->bo[1])
		return -ENOMEM;

	for (i = 0; i < size; i += 4)
		nv_wv32(pgt->bo[1], i, 0);

	pde[0] = pgt->limit << 2;
	pde[1] = (pgt->bo[1]->start >> 8) | 1;

	if (vs->vid != -3) {
		pgt->bo[0] = pscnv_mem_alloc(vs->dev, NVC0_VM_LPTE_COUNT * 8,
					      PSCNV_GEM_CONTIG, 0, 0x79, NULL);
		if (!pgt->bo[0])
			return -ENOMEM;

		nvc0_vm_map_kernel(pgt->bo[0]);
		nvc0_vm_map_kernel(pgt->bo[1]);

		for (i = 0; i < NVC0_VM_LPTE_COUNT * 8; i += 4)
			nv_wv32(pgt->bo[0], i, 0);

		pde[0] |= (pgt->bo[0]->start >> 8) | 1;
	}
	dev_priv->vm->bar_flush(vs->dev);
	
	spin_lock_irqsave(&nvc0_vs(vs)->pd_lock, flags);
	nv_wv32(nvc0_vs(vs)->pd, pgt->pde * 8 + 0, pde[0]);
	nv_wv32(nvc0_vs(vs)->pd, pgt->pde * 8 + 4, pde[1]);
	spin_unlock_irqrestore(&nvc0_vs(vs)->pd_lock, flags);
	
	dev_priv->vm->bar_flush(vs->dev);
	return nvc0_tlb_flush(vs);
}

static struct nvc0_pgt *
nvc0_vspace_pgt_or_null(struct pscnv_vspace *vs, unsigned int pde)
{
	struct nvc0_pgt *pt;
	struct list_head *pts = &nvc0_vs(vs)->ptht[NVC0_PDE_HASH(pde)];

	BUG_ON(pde >= NVC0_VM_PDE_COUNT);

	list_for_each_entry(pt, pts, head)
		if (pt->pde == pde)
			return pt;
	
	return NULL;
}

static struct nvc0_pgt *
nvc0_vspace_pgt(struct pscnv_vspace *vs, unsigned int pde)
{
	struct nvc0_pgt *pt;
	struct list_head *pts = &nvc0_vs(vs)->ptht[NVC0_PDE_HASH(pde)];
	
	if ((pt = nvc0_vspace_pgt_or_null(vs, pde))) {
		return pt;
	}

	if (pscnv_vm_debug >= 2) {
		NV_INFO(vs->dev, "creating new page table: %i[%u]\n", vs->vid, pde);
	}

	pt = kzalloc(sizeof *pt, GFP_KERNEL);
	if (!pt)
		return NULL;
	pt->pde = pde;
	pt->limit = 0;

	if (nvc0_vspace_fill_pde(vs, pt)) {
		kfree(pt);
		return NULL;
	}

	list_add_tail(&pt->head, pts);
	return pt;
}

static void
nvc0_pgt_del(struct pscnv_vspace *vs, struct nvc0_pgt *pgt)
{
	unsigned long flags;
	
	pscnv_vram_free(pgt->bo[1]);
	if (pgt->bo[0])
		pscnv_vram_free(pgt->bo[0]);
	list_del(&pgt->head);

	spin_lock_irqsave(&nvc0_vs(vs)->pd_lock, flags);
	nv_wv32(nvc0_vs(vs)->pd, pgt->pde * 8 + 0, 0);
	nv_wv32(nvc0_vs(vs)->pd, pgt->pde * 8 + 4, 0);
	spin_unlock_irqrestore(&nvc0_vs(vs)->pd_lock, flags);

	kfree(pgt);
}

static int
nvc0_vspace_do_unmap(struct pscnv_vspace *vs, uint64_t offset, uint64_t size)
{
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	uint32_t space;

	for (; size; offset += space) {
		struct nvc0_pgt *pt;
		int i, pte;

		pt = nvc0_vspace_pgt(vs, NVC0_PDE(offset));
		space = NVC0_VM_BLOCK_SIZE - (offset & NVC0_VM_BLOCK_MASK);
		if (space > size)
			space = size;
		size -= space;

		pte = NVC0_SPTE(offset);
		for (i = 0; i < (space >> NVC0_SPAGE_SHIFT) * 8; i += 4)
			nv_wv32(pt->bo[1], pte * 8 + i, 0);

		if (!pt->bo[0])
			continue;

		pte = NVC0_LPTE(offset);
		for (i = 0; i < (space >> NVC0_LPAGE_SHIFT) * 8; i += 4)
			nv_wv32(pt->bo[0], pte * 8 + i, 0);
	}
	dev_priv->vm->bar_flush(vs->dev);
	return nvc0_tlb_flush(vs);
}

static inline void
write_pt(struct pscnv_bo *pt, int pte, int count, uint64_t phys,
	 int psz, uint32_t pfl0, uint32_t pfl1)
{
	int i;
	uint32_t a = (phys >> 8) | pfl0;
	uint32_t b = pfl1;

	psz >>= 8;

	for (i = pte * 8; i < (pte + count) * 8; i += 8, a += psz) {
		nv_wv32(pt, i + 4, b);
		nv_wv32(pt, i + 0, a);
	}
}

static const char *
nvc0_vm_storage_type_str(int type)
{
	switch (type) {
		case 0x0: return "VRAM";
		case 0x5: return "SYSRAM_SNOOP";
		case 0x7: return "SYSRAM_NOSNOOP";
	}
	
	return "UNKNOWN TYPE";
}

static void
nvc0_vm_pt_dump(struct drm_device *dev, uint64_t pt_addr, int id, int small, int limit, int pde, int *entrycnt)
{
	uint32_t size;
	const char *type_str = (small) ? "SPT" : "LPT";
	int i;
	
	if (small) {
		size = NVC0_VM_SPTE_COUNT >> limit;
	} else {
		size = NVC0_VM_LPTE_COUNT;
	}
	
	for (i = 0; i < size; ) {
		
		unsigned i_start = i;
		uint32_t a, b;
		uint64_t start, end;
		int sysflag, type, valid;
		
		uint32_t a_next, b_next;
		uint64_t addr_next;
		int sysflag_next, type_next, valid_next;
		
		a = nv_rv32_pramin(dev, pt_addr + i * 8);
		b = nv_rv32_pramin(dev, pt_addr + i * 8 + 4);
		
		start = end = a >> 4;
		sysflag = (a >> 1) & 1;
		type = b & 7;
		valid = a & 1;
		
		if (!valid) {
			i++;
			continue;
		}
		
		do {
			end += (small) ? 1 : 32;
			i++;
			
			if (i >= size) {
				break;
			}
			
			a_next = nv_rv32_pramin(dev, pt_addr + i * 8);
			b_next = nv_rv32_pramin(dev, pt_addr + i * 8 + 4);
			
			valid_next = a_next & 1;
			addr_next = a_next >> 4;
			sysflag_next = (a_next >> 1) & 1;
			type_next = b_next & 7;
			
		} while (valid_next && addr_next == end && sysflag_next == sysflag && type_next == type);
		
		NV_INFO(dev, "%d[%d]%s   %04x: %04llx-%04llx sys=%d %s\n",
			id, pde, type_str, i_start, start, end, sysflag, nvc0_vm_storage_type_str(type));
		
		*entrycnt += 1;
			
		/* we seem to read bullshit */
		if (*entrycnt >= 1000) {
			return;
		}
	}
}

/* pd_addr = physical address of page directory in VRAM */
static void
nvc0_vm_pd_dump(struct drm_device *dev, uint64_t pd_addr, int id)
{
	unsigned int i;
	int entrycnt = 0;
	
	for (i = 0; i < NVC0_VM_PDE_COUNT; i++) {
		uint32_t a, b;
		uint32_t lpt_addr, spt_addr, spt_limit, spt_valid, lpt_valid;
		
		a = nv_rv32_pramin(dev, pd_addr + i * 8);
		b = nv_rv32_pramin(dev, pd_addr + i * 8 + 4);
		
		spt_valid = b & 1;
		lpt_valid = a & 1;
		spt_limit = (a >> 2) & 3;
		lpt_addr = (a >> 4);
		spt_addr = (b >> 4);
		
		if (a != 0 || b != 0) {
			NV_INFO(dev, "%d[%d] ** LPT=%08x SPT_LIMIT=%d LPT_VALID=%d SPT=%08x SPT_VALID=%d\n",
				id, i, lpt_addr, spt_limit, lpt_valid, spt_addr, spt_valid);
		}
		if (spt_valid) {	
			nvc0_vm_pt_dump(dev, spt_addr << 12, id, true, spt_limit, i, &entrycnt);
		}
		if (lpt_valid) {
			nvc0_vm_pt_dump(dev, lpt_addr << 12, id, false, 0, i, &entrycnt);
		}
		
		/* we seem to read bullshit */
		if (entrycnt >= 1000) {
			return;
		}
	}
}

static int
nvc0_vspace_place_map (struct pscnv_vspace *vs, struct pscnv_bo *bo,
		       uint64_t start, uint64_t end, int back,
		       struct pscnv_mm_node **res)
{
	int flags = 0;

	if ((bo->flags & PSCNV_GEM_MEMTYPE_MASK) == PSCNV_GEM_VRAM_LARGE)
		flags = PSCNV_MM_LP;
	if (back)
		flags |= PSCNV_MM_FROMBACK;

	return pscnv_mm_alloc(vs->mm, bo->size, flags, start, end, res);
}

static int
nvc0_vspace_do_map(struct pscnv_vspace *vs,
		   struct pscnv_bo *bo, uint64_t offset)
{
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	uint32_t pfl0, pfl1;
	struct pscnv_mm_node *reg;
	int i;
	unsigned long flags;
	int s;
	uint32_t psh, psz;

	pfl0 = 1;
	if (vs->vid >= 0 && (bo->flags & PSCNV_GEM_NOUSER))
		pfl0 |= 2;

	pfl1 = bo->tile_flags << 4;

	switch (bo->flags & PSCNV_GEM_MEMTYPE_MASK) {
	case PSCNV_GEM_SYSRAM_NOSNOOP:
		pfl1 |= 0x2;
		/* fall through */
	case PSCNV_GEM_SYSRAM_SNOOP:
	{
		unsigned int pde = NVC0_PDE(offset);
		unsigned int pte = (offset & NVC0_VM_BLOCK_MASK) >> PAGE_SHIFT;
		struct nvc0_pgt *pt = nvc0_vspace_pgt(vs, pde);
		pfl1 |= 0x5;
		spin_lock_irqsave(&nvc0_vs(vs)->pd_lock, flags);
		for (i = 0; i < (bo->size >> PAGE_SHIFT); ++i) {
			uint64_t phys = bo->dmapages[i];
			nv_wv32(pt->bo[1], pte * 8 + 4, pfl1);
			nv_wv32(pt->bo[1], pte * 8 + 0, (phys >> 8) | pfl0);
			pte++;
			if ((pte & (NVC0_VM_BLOCK_MASK >> PAGE_SHIFT)) == 0) {
				spin_unlock_irqrestore(&nvc0_vs(vs)->pd_lock, flags);
				pte = 0;
				pt = nvc0_vspace_pgt(vs, ++pde);
				spin_lock_irqsave(&nvc0_vs(vs)->pd_lock, flags);
			}
		}
		spin_unlock_irqrestore(&nvc0_vs(vs)->pd_lock, flags);
	}
		break;
	case PSCNV_GEM_VRAM_SMALL:
	case PSCNV_GEM_VRAM_LARGE:
		s = (bo->flags & PSCNV_GEM_MEMTYPE_MASK) != PSCNV_GEM_VRAM_LARGE;
		if (vs->vid == -3)
			s = 1;
		psh = s ? NVC0_SPAGE_SHIFT : NVC0_LPAGE_SHIFT;
		psz = 1 << psh;
		
		for (reg = bo->mmnode; reg; reg = reg->next) {
			uint64_t phys = reg->start;
			uint64_t size = reg->size;

			while (size) {
				struct nvc0_pgt *pt;
				int pte, count;
				uint32_t space;

				space = NVC0_VM_BLOCK_SIZE -
					(offset & NVC0_VM_BLOCK_MASK);
				if (space > size)
					space = size;
				size -= space;

				pte = (offset & NVC0_VM_BLOCK_MASK) >> psh;
				count = space >> psh;
				pt = nvc0_vspace_pgt(vs, NVC0_PDE(offset));

				spin_lock_irqsave(&nvc0_vs(vs)->pd_lock, flags);
				write_pt(pt->bo[s], pte, count, phys, psz, pfl0, pfl1);
				spin_unlock_irqrestore(&nvc0_vs(vs)->pd_lock, flags);

				offset += space;
				phys += space;
			}
		}
		break;
	default:
		WARN(1, "Should not be here! Mask %08x\n", bo->flags & PSCNV_GEM_MEMTYPE_MASK);
		return -ENOSYS;
	}
	dev_priv->vm->bar_flush(vs->dev);
	return nvc0_tlb_flush(vs);
}

static int nvc0_vspace_new(struct pscnv_vspace *vs) {
	unsigned long flags;
	int i, ret;

	if (vs->size > 1ull << 40)
		return -EINVAL;

	vs->engdata = kzalloc(sizeof(struct nvc0_vspace), GFP_KERNEL);
	if (!vs->engdata) {
		NV_ERROR(vs->dev, "VM: Couldn't alloc vspace eng\n");
		return -ENOMEM;
	}
	
	spin_lock_init(&nvc0_vs(vs)->pd_lock);

	nvc0_vs(vs)->pd = pscnv_mem_alloc(vs->dev, NVC0_VM_PDE_COUNT * 8,
			PSCNV_GEM_CONTIG, 0, 0xdeadcafe, NULL);
	if (!nvc0_vs(vs)->pd) {
		NV_ERROR(vs->dev, "VM: nvc0_vspace_new: pscnv_mem_alloc failed\n");
		kfree(vs->engdata);
		return -ENOMEM;
	}

	if (vs->vid != -3)
		nvc0_vm_map_kernel(nvc0_vs(vs)->pd);
	
	spin_lock_irqsave(&nvc0_vs(vs)->pd_lock, flags);
	for (i = 0; i < NVC0_VM_PDE_COUNT; i++) {
		nv_wv32(nvc0_vs(vs)->pd, i * 8, 0);
		nv_wv32(nvc0_vs(vs)->pd, i * 8 + 4, 0);
	}
	spin_unlock_irqrestore(&nvc0_vs(vs)->pd_lock, flags);
	
	for (i = 0; i < NVC0_PDE_HT_SIZE; ++i)
		INIT_LIST_HEAD(&nvc0_vs(vs)->ptht[i]);

	ret = pscnv_mm_init(vs->dev, 0, vs->size, 0x1000, 0x20000, 1, &vs->mm);
	if (ret) {
		pscnv_mem_free(nvc0_vs(vs)->pd);
		kfree(vs->engdata);
	}
	return ret;
}

static void nvc0_vspace_free(struct pscnv_vspace *vs) {
	int i;
	for (i = 0; i < NVC0_PDE_HT_SIZE; i++) {
		struct nvc0_pgt *pgt, *save;
		list_for_each_entry_safe(pgt, save, &nvc0_vs(vs)->ptht[i], head)
			nvc0_pgt_del(vs, pgt);
	}
	pscnv_mem_free(nvc0_vs(vs)->pd);

	kfree(vs->engdata);
}

static int nvc0_vm_map_user(struct pscnv_bo *bo) {
	struct drm_nouveau_private *dev_priv = bo->dev->dev_private;
	struct nvc0_vm_engine *vme = nvc0_vm(dev_priv->vm);
	if (bo->map1)
		return 0;
	return pscnv_vspace_map(vme->bar1vm, bo, 0, dev_priv->fb_size, 0, &bo->map1);
}

static int nvc0_vm_map_kernel(struct pscnv_bo *bo) {
	struct drm_nouveau_private *dev_priv = bo->dev->dev_private;
	struct nvc0_vm_engine *vme = nvc0_vm(dev_priv->vm);
	if (bo->map3)
		return 0;
	return pscnv_vspace_map(vme->bar3vm, bo, 0, dev_priv->ramin_size, 0, &bo->map3);
}

static void
nvc0_vm_pd_dump_bar(struct drm_device *dev, int bar, uint32_t reg)
{
	uint64_t pd_addr;
	uint64_t chan_bo_addr;
	
	chan_bo_addr = (uint64_t)(nv_rd32(dev, reg) & 0x3FFFFF) << 12;
	
	if (!chan_bo_addr) {
		NV_INFO(dev, "nvc0_vm_pd_dump_bar%d: no channel BO\n", bar);
		return;
	}
	
	pd_addr = nv_rv32_pramin(dev, chan_bo_addr + 0x200);
	pd_addr |= (uint64_t)(nv_rv32_pramin(dev, chan_bo_addr + 0x204)) << 32;
	
	if (!pd_addr) {
		NV_ERROR(dev, "nvc0_vm_pd_dump_bar%d: channel BO exists at 0x%08llx"
			      ", but no PD, wtf\n", bar, chan_bo_addr);
		return;
	}
	
	NV_INFO(dev, "DUMP BAR%d PD at 0x%08llx\n", bar, pd_addr);
	
	nvc0_vm_pd_dump(dev, pd_addr, -bar);
}

static void
nvc0_vm_pd_dump_bar3(struct drm_device *dev)
{
	nvc0_vm_pd_dump_bar(dev, 3, 0x1714);
}

static void
nvc0_vm_pd_dump_bar1(struct drm_device *dev)
{
	nvc0_vm_pd_dump_bar(dev, 1, 0x1704);
}

int
nvc0_vm_init(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_pgt *pt;
	struct nvc0_vm_engine *vme = kzalloc(sizeof *vme, GFP_KERNEL);
	if (!vme) {
		NV_ERROR(dev, "VM: Couldn't alloc engine\n");
		return -ENOMEM;
	}
	vme->base.takedown = nvc0_vm_takedown;
	vme->base.do_vspace_new = nvc0_vspace_new;
	vme->base.do_vspace_free = nvc0_vspace_free;
	vme->base.place_map = nvc0_vspace_place_map;
	vme->base.do_map = nvc0_vspace_do_map;
	vme->base.do_unmap = nvc0_vspace_do_unmap;
	vme->base.map_user = nvc0_vm_map_user;
	vme->base.map_kernel = nvc0_vm_map_kernel;
	vme->base.bar_flush = nv84_vm_bar_flush;
	vme->base.pd_dump = nvc0_vm_pd_dump;
	vme->base.pd_dump_bar1 = nvc0_vm_pd_dump_bar1;
	vme->base.pd_dump_bar3 = nvc0_vm_pd_dump_bar3;
	dev_priv->vm = &vme->base;

	dev_priv->vm_ramin_base = 0;
	spin_lock_init(&dev_priv->vm->vs_lock);

	/*nv_wr32(dev, 0x200, 0xfffffeff);
	nv_wr32(dev, 0x200, 0xffffffff);

	nv_wr32(dev, 0x100c80, 0x00208000);*/

	vme->bar3vm = pscnv_vspace_new (dev, dev_priv->ramin_size, 0, 3);
	if (!vme->bar3vm) {
		NV_ERROR(dev, "VM: failed to create BAR3 VM");
		kfree(vme);
		dev_priv->vm = 0;
		return -ENOMEM;
	}
	/* this channel is not yet mapped into it's VS */
	vme->bar3ch = pscnv_chan_new (dev, vme->bar3vm, 3);
	if (!vme->bar3ch) {
		NV_ERROR(dev, "VM: failed to create BAR3 Channel");
		pscnv_vspace_unref(vme->bar3vm);
		kfree(vme);
		dev_priv->vm = 0;
		return -ENOMEM;
	}
	
	nv_mask(dev, 0x000200, 0x00000100, 0x00000000);
	nv_mask(dev, 0x000200, 0x00000100, 0x00000100);
	nv_mask(dev, 0x100c80, 0x00000001, 0x00000000);
	
	nv_wr32(dev, 0x1714, 0xc0000000 | vme->bar3ch->bo->start >> 12);

	dev_priv->vm_ok = 1;

	nvc0_vm_map_kernel(vme->bar3ch->bo);
	nvc0_vm_map_kernel(nvc0_vs(vme->bar3vm)->pd);
	pt = nvc0_vspace_pgt(vme->bar3vm, 0);
	if (!pt) {
		NV_ERROR(dev, "VM: failed to allocate RAMIN page table\n");
		return -ENOMEM;
	}
	nvc0_vm_map_kernel(pt->bo[1]);

	vme->bar1vm = pscnv_vspace_new (dev, dev_priv->fb_size, 0, 1);
	if (!vme->bar1vm) {
		dev_priv->vm_ok = 0;
		pscnv_chan_unref(vme->bar3ch);
		pscnv_vspace_unref(vme->bar3vm);
		kfree(vme);
		dev_priv->vm = 0;
		return -ENOMEM;
	}
	vme->bar1ch = pscnv_chan_new (dev, vme->bar1vm, 1);
	if (!vme->bar1ch) {
		dev_priv->vm_ok = 0;
		pscnv_vspace_unref(vme->bar1vm);
		pscnv_chan_unref(vme->bar3ch);
		pscnv_vspace_unref(vme->bar3vm);
		kfree(vme);
		dev_priv->vm = 0;
		return -ENOMEM;
	}
	nv_wr32(dev, 0x1704, 0x80000000 | vme->bar1ch->bo->start >> 12);
	return 0;
}

void
nvc0_vm_takedown(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_vm_engine *vme = nvc0_vm(dev_priv->vm);
	/* XXX: write me. */
	dev_priv->vm_ok = 0;
	nv_wr32(dev, 0x1704, 0);
	nv_wr32(dev, 0x1714, 0);
	nv_wr32(dev, 0x1718, 0);
	pscnv_chan_unref(vme->bar1ch);
	pscnv_vspace_unref(vme->bar1vm);
	pscnv_chan_unref(vme->bar3ch);
	pscnv_vspace_unref(vme->bar3vm);
	kfree(vme);
	dev_priv->vm = 0;
}
