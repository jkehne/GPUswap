/*
 * Copyright (C) 2010 Christoph Bumiller.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "nvc0_fifo.h"
#include "nouveau_reg.h"
#include "pscnv_chan.h"
#include "nvc0_vm.h"
#include "pscnv_ib_chan.h"

static void
nvc0_fifo_takedown(struct pscnv_engine *eng);

static void
nvc0_fifo_irq_handler(struct drm_device *dev, int irq);

static int
nvc0_fifo_chan_init_ib (struct pscnv_chan *ch, uint32_t pb_handle, uint32_t flags, uint32_t slimask, uint64_t ib_start, uint32_t ib_order);

static void
nvc0_fifo_chan_kill(struct pscnv_engine *eng, struct pscnv_chan *ch);

static void
nvc0_fifo_chan_free(struct pscnv_engine *eng, struct pscnv_chan *ch);

static void
nvc0_fifo_playlist_update(struct drm_device *dev);

/*******************************************************************************
 * PFIFO channel control
 ******************************************************************************/

static uint64_t
nvc0_fifo_get_fifo_regs(struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	
	return fifo->ctrl_bo->start + (ch->cid << 12);
}

static int
nvc0_fifo_chan_init_ib (struct pscnv_chan *ch, uint32_t pb_handle, uint32_t flags, uint32_t slimask, uint64_t ib_start, uint32_t ib_order) {
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	struct nvc0_fifo_ctx *fifo_ctx;
	struct pscnv_bo *ib;
	unsigned long irqflags;
	enum pscnv_chan_state st;
	int ret;

	int i;
	uint64_t fifo_regs = nvc0_fifo_get_fifo_regs(ch);

	if (ib_order != 9) {
		NV_ERROR(dev, "nvc0_fifo_chan_init_ib: ib_order=%d requested, "
			"but only ib_order=9 supported atm\n", ib_order);
		return -EINVAL;
	}
	
	st = pscnv_chan_get_state(ch);
	if (st != PSCNV_CHAN_INITIALIZED) {
		NV_ERROR(dev, "nvc0_fifo_chan_init_ib: channel %d in unexpected"
			" state %s\n", ch->cid, pscnv_chan_state_str(st));
		return -EINVAL;
	}
	
	ib = pscnv_vspace_vm_addr_lookup(ch->vspace, ib_start);
	if (!ib) {
		NV_ERROR(dev, "nvc0_fifo_chan_init_ib: 0x%llx in vspace %d given"
			" as start address for indirect buffer of channel %d,"
			" but no BO mapped there\n", ib_start, ch->vspace->vid,
			ch->cid);
		return -EINVAL;
	}
	if (ib->size != 8*(1ULL << ib_order)) {
		NV_ERROR(dev, "nvc0_fifo_chan_init_ib: IB at BO %08x/%d has "
			"size 0x%llx, but expected 0x%llx\n",
			ib->cookie, ib->serial,	ib->size, 8*(1ULL << ib_order));
		return -EINVAL;
	}
	
	ib->flags |= PSCNV_GEM_IB;
	
	fifo_ctx = kmalloc(sizeof(*fifo_ctx), GFP_KERNEL);
	if (!fifo_ctx) {
		NV_ERROR(dev, "nvc0_fifo_chan_init_ib: out of memory\n");
		return -ENOMEM;
	}
	
	fifo_ctx->ib = ib;
	pscnv_bo_ref(ib);

	spin_lock_irqsave(&dev_priv->context_switch_lock, irqflags);

	for (i = 0; i < 0x1000; i += 4) {
		nv_wv32(fifo->ctrl_bo, (ch->cid << 12) + i, 0);
	}

	for (i = 0; i < 0x100; i += 4)
		nv_wv32(ch->bo, i, 0);

	dev_priv->vm->bar_flush(dev);

	nv_wv32(ch->bo, 0x08, fifo_regs);
	nv_wv32(ch->bo, 0x0c, fifo_regs >> 32);

	nv_wv32(ch->bo, 0x48, ib_start); /* IB */
	nv_wv32(ch->bo, 0x4c,
		(ib_start >> 32) | (ib_order << 16));
	nv_wv32(ch->bo, 0x10, 0xface);
	nv_wv32(ch->bo, 0x54, 0x2);
	nv_wv32(ch->bo, 0x9c, 0x100);
	nv_wv32(ch->bo, 0x84, 0x20400000);
	nv_wv32(ch->bo, 0x94, 0x30000001);
	nv_wv32(ch->bo, 0xa4, 0x1f1f1f1f);
	nv_wv32(ch->bo, 0xa8, 0x1f1f1f1f);
	nv_wv32(ch->bo, 0xac, 0x1f);
	nv_wv32(ch->bo, 0x30, 0xfffff902);
	nv_wv32(ch->bo, 0xb8, 0xf8000000); /* previously omitted */
	nv_wv32(ch->bo, 0xf8, 0x10003080);
	nv_wv32(ch->bo, 0xfc, 0x10000010);
	dev_priv->vm->bar_flush(dev);

	nv_wr32(dev, 0x3000 + ch->cid * 8, 0xc0000000 | ch->bo->start >> 12);
	nv_wr32(dev, 0x3004 + ch->cid * 8, 0x1f0001);

	nvc0_fifo_playlist_update(dev);

	spin_unlock_irqrestore(&dev_priv->context_switch_lock, irqflags);

	ch->engdata[PSCNV_ENGINE_FIFO] = fifo_ctx;
	
	dev_priv->engines[PSCNV_ENGINE_GRAPH]->
		chan_alloc(dev_priv->engines[PSCNV_ENGINE_GRAPH], ch);
	if (dev_priv->engines[PSCNV_ENGINE_COPY0])
		dev_priv->engines[PSCNV_ENGINE_COPY0]->
			chan_alloc(dev_priv->engines[PSCNV_ENGINE_COPY0], ch);
	if (dev_priv->engines[PSCNV_ENGINE_COPY1])
		dev_priv->engines[PSCNV_ENGINE_COPY1]->
			chan_alloc(dev_priv->engines[PSCNV_ENGINE_COPY1], ch);

	pscnv_chan_set_state(ch, PSCNV_CHAN_RUNNING);
	
	fifo_ctx->ib_chan = pscnv_ib_chan_init(ch);
	if (!fifo_ctx->ib_chan) {
		NV_ERROR(dev, "nvc0_fifo_chan_init_ib: failed to allocate "
			"ib_chan on channel %d\n", ch->cid);
		pscnv_chan_fail(ch);
		return -EFAULT;
	}
	
	ret = pscnv_ib_add_fence(fifo_ctx->ib_chan);
	if (ret) {
		NV_ERROR(dev, "nvc0_fifo_chan_init_ib: failed to allocate "
			"fence on channel %d\n", ch->cid);
		pscnv_chan_fail(ch);
		return ret;
	}
	
	return 0;
}

static void
nvc0_fifo_chan_kill(struct pscnv_engine *eng, struct pscnv_chan *ch)
{
	struct drm_device *dev = ch->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_fifo_ctx *fifo_ctx = ch->engdata[PSCNV_ENGINE_FIFO];
	
	uint32_t status;
	unsigned long flags;
	
	BUG_ON(!fifo_ctx);
	
	/* bit 28: active,
	 * bit 12: loaded,
	 * bit  0: enabled
	 */
	
	pscnv_ib_chan_kill(fifo_ctx->ib_chan);

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	status = nv_rd32(dev, 0x3004 + ch->cid * 8);
	nv_wr32(dev, 0x3004 + ch->cid * 8, status & ~1);
	nv_wr32(dev, 0x2634, ch->cid);
	if (!nv_wait(dev, 0x2634, ~0, ch->cid))
		NV_WARN(dev, "WARNING: 2634 = 0x%08x\n", nv_rd32(dev, 0x2634));

	nvc0_fifo_playlist_update(dev);

	if (nv_rd32(dev, 0x3004 + ch->cid * 8) & 0x1110) {
		NV_WARN(dev, "WARNING: PFIFO kickoff fail :(\n");
	}
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);
}

static void
nvc0_fifo_chan_free(struct pscnv_engine *eng, struct pscnv_chan *ch)
{
	struct nvc0_fifo_ctx *fifo_ctx = ch->engdata[PSCNV_ENGINE_FIFO];

	pscnv_bo_unref(fifo_ctx->ib);
	kfree(fifo_ctx);
	ch->engdata[PSCNV_ENGINE_FIFO] = NULL;
}

uint64_t
nvc0_fifo_ctrl_offs(struct drm_device *dev, int cid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	return fifo->ctrl_bo->map1->start + cid * 0x1000;
}

/*******************************************************************************
 * PFIFO playlist management
 ******************************************************************************/

static void
nvc0_fifo_playlist_update(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(dev_priv->fifo);
	int i, pos;
	struct pscnv_bo *vo;
	fifo->cur_playlist ^= 1;
	vo = fifo->playlist[fifo->cur_playlist];
	for (i = 0, pos = 0; i < 128; i++) {
		if (nv_rd32(dev, 0x3004 + i * 8) & 1) {
			nv_wv32(vo, pos, i);
			nv_wv32(vo, pos + 4, 0x4);
			pos += 8;
		}
	}
	dev_priv->vm->bar_flush(dev);

	nv_wr32(dev, 0x2270, vo->start >> 12);
	nv_wr32(dev, 0x2274, 0x1f00000 | pos / 8);

	if (!nv_wait(dev, 0x227c, (1 << 20), 0))
		NV_WARN(dev, "WARNING: PFIFO 227c = 0x%08x\n",
			nv_rd32(dev, 0x227c));
}

/*******************************************************************************
 * PFIFO interrupt handling
 ******************************************************************************/

static const char *
pgf_unit_str(int unit)
{
	switch (unit) {
	case 0x00: return "PGRAPH";
	case 0x03: return "PEEPHOLE";
	case 0x04: return "FB BAR (BAR1)";
	case 0x05: return "RAMIN BAR (BAR3)";
	case 0x07: return "PFIFO";
	case 0x10: return "PBSP";
	case 0x11: return "PPPP";
	case 0x13: return "PCOUNTER";
	case 0x14: return "PVP";
	case 0x15: return "PCOPY0";
	case 0x16: return "PCOPY1";
	case 0x17: return "PDAEMON";
	
	default:
		break;
	}
	return "(unknown unit)";
}

static const char *
pgf_cause_str(uint32_t flags)
{
	switch (flags & 0xf) {
	case 0x0: return "PDE not present";
	case 0x1: return "PT too short";
	case 0x2: return "PTE not present";
	case 0x3: return "VM LIMIT exceeded";
	case 0x4: return "NO CHANNEL";
	case 0x5: return "PAGE SYSTEM ONLY";
	case 0x6: return "PTE set read-only";
	case 0xa: return "Compressed Sysram";
	case 0xc: return "Invalid Storage Type";
	default:
		break;
	}
	return "unknown cause";
}

static const char *
fifo_sched_cause_str(uint32_t flags)
{
	switch (flags & 0xff) {
	case 0xa: return "CTXSW_TIMEOUT";
	default:
		break;
	}
	return "unknown cause";
}

static void
nvc0_pfifo_page_fault(struct drm_device *dev, int unit)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	uint64_t virt;
	uint32_t inst, flags;
	int chid;
	struct pscnv_chan* ch;
	
	/* this forces nv_rv32 to use "slowpath" pramin access. 
	 *
	 * this ensures that we can still read the contents of BOs, even if
	 * one of the BAR's pagefaulted  */
	dev_priv->vm_ok = false;

	inst = nv_rd32(dev, 0x2800 + unit * 0x10);
	chid = pscnv_chan_handle_lookup(dev, inst);
	virt = nv_rd32(dev, 0x2808 + unit * 0x10);
	virt = (virt << 32) | nv_rd32(dev, 0x2804 + unit * 0x10);
	flags = nv_rd32(dev, 0x280c + unit * 0x10);

	NV_INFO(dev, "channel %d: %s PAGE FAULT at 0x%010llx (%c, %s)\n",
		chid, pgf_unit_str(unit), virt,
		(flags & 0x80) ? 'w' : 'r', pgf_cause_str(flags));
	
	ch = pscnv_chan_chid_lookup(dev, chid);
	if (!ch) {
		pscnv_chan_fail(ch);
	}
	
	if ((unit == 0x05 || chid == -3) && dev_priv->vm->pd_dump_bar3) {
		dev_priv->vm->pd_dump_bar3(dev, NULL);
	} else if ((unit == 0x04 || chid == -1) && dev_priv->vm->pd_dump_bar1) {
		dev_priv->vm->pd_dump_bar1(dev, NULL);
	} else if (1 <= chid && chid <= 127 && dev_priv->chan->pd_dump_chan) {
		dev_priv->chan->pd_dump_chan(dev, NULL, chid);
	}
}

static void
nvc0_pfifo_subfifo_fault(struct drm_device *dev, int unit)
{
	int cid = nv_rd32(dev, 0x40120 + unit * 0x2000) & 0x7f;
	int status = nv_rd32(dev, 0x40108 + unit * 0x2000);
	uint32_t addr = nv_rd32(dev, 0x400c0 + unit * 0x2000);
	uint32_t data = nv_rd32(dev, 0x400c4 + unit * 0x2000);
	int sub = addr >> 16 & 7;
	int mthd = addr & 0x3ffc;
	int mode = addr >> 21 & 7;

	if (status & 0x200000) {
		NV_INFO(dev, "PSUBFIFO %d ILLEGAL_MTHD: ch %d sub %d mthd %04x%s [mode %d] data %08x\n", unit, cid, sub, mthd, ((addr & 1)?" NI":""), mode, data);
		nv_wr32(dev, 0x400c0 + unit * 0x2000, 0x80600008);
		nv_wr32(dev, 0x40108 + unit * 0x2000, 0x200000);
		status &= ~0x200000;
	}
	if (status & 0x800000) {
		NV_INFO(dev, "PSUBFIFO %d EMPTY_SUBCHANNEL: ch %d sub %d mthd %04x%s [mode %d] data %08x\n", unit, cid, sub, mthd, ((addr & 1)?" NI":""), mode, data);
		nv_wr32(dev, 0x400c0 + unit * 0x2000, 0x80600008);
		nv_wr32(dev, 0x40108 + unit * 0x2000, 0x800000);
		status &= ~0x800000;
	}
	if (status) {
		NV_INFO(dev, "unknown PSUBFIFO INTR: 0x%08x\n", status);
		nv_wr32(dev, 0x4010c + unit * 0x2000, nv_rd32(dev, 0x4010c + unit * 0x2000) & ~status);
	}
}

static void
nvc0_fifo_irq_handler(struct drm_device *dev, int irq)
{
	static int num_fuckups = 0;
	static int num_oxo1 = 0;
	uint32_t status;

	status = nv_rd32(dev, 0x2100) & nv_rd32(dev, 0x2140);

	if (status & 0x00000001) {
		u32 intr = nv_rd32(dev, 0x00252c);
		NV_INFO(dev, "PFIFO INTR 0x00000001 (Puller error?): 0x%08x\n", intr);
		nv_wr32(dev, 0x002100, 0x00000001); /* ack */
		status &= ~0x00000001;
	}

	/* this interrupt meight be thrown with intr==5, when the nvidia card is
	 * not the primary gpu (BIOS setup). */
	if (status & 0x01000000) {
		u32 intr = nv_rd32(dev, 0x00258c);
		
		num_oxo1++;
		
		/* don't pollute the terminal */
		if (num_oxo1 < 10) {
			NV_INFO(dev, "INTR 0x01000000: 0x%08x\n", intr);
		}
		
		if (num_oxo1 == 10) {
			NV_INFO(dev, "too many INTR 0x01000000, disabling this interrupt\n");
			nv_wr32(dev, 0x2140, nv_rd32(dev, 0x2140) & ~0x01000000);
		}
		
		if (num_oxo1 > 10 && num_oxo1 < 100) {
			NV_ERROR(dev, "disabled INTR 0x01000000, but still thrown");
		}
		
		nv_wr32(dev, 0x002100, 0x01000000); /* ack */
		status &= ~0x01000000;
	}
	
	/* for comparsion with nouveau:
	 * __ffs(0) is undefined, forall n>0: __ffs(n) == ffs(n) - 1 */
	
	if (status & 0x10000000) {
		uint32_t bits = nv_rd32(dev, 0x259c);
		uint32_t units = bits;

		while (units) {
			int i = ffs(units) - 1;
			units &= ~(1 << i);
			nvc0_pfifo_page_fault(dev, i);
		}
		nv_wr32(dev, 0x259c, bits); /* ack */
		status &= ~0x10000000;
	}

	if (status & 0x20000000) {
		uint32_t bits = nv_rd32(dev, 0x25a0);
		uint32_t units = bits;
		while (units) {
			int i = ffs(units) - 1;
			units &= ~(1 << i);
			nvc0_pfifo_subfifo_fault(dev, i);
		}
		nv_wr32(dev, 0x25a0, bits); /* ack */
		status &= ~0x20000000;
	}
	
	if (status & 0x40000000) {
		uint32_t intr = nv_rd32(dev, 0x2a00);

		/* nouveau stuff, pscnv doesn't seem to know this
		if (intr & 0x10000000) {
			wake_up(&priv->runlist.wait);
			nv_wr32(priv, 0x002a00, 0x10000000);
			intr &= ~0x10000000;
		}*/

		NV_INFO(dev, "RUNLIST 0x%08x\n", intr);
		status &= ~0x40000000;
	}

	if (status & 0x80000000) {
		NV_INFO(dev, "ENGINE INTERRUPT\n");
		status &= ~0x80000000;
	}

	if (status & 0x00000100) {
		uint32_t ibpk[2];
		uint32_t data = nv_rd32(dev, 0x400c4);
		uint32_t code = nv_rd32(dev, 0x254c);
		uint32_t chan_id = nv_rd32(dev, 0x2640) & 0x7f; /* usual chid */
		const char *cause = fifo_sched_cause_str(code);
		
		num_fuckups++;

		ibpk[0] = nv_rd32(dev, 0x40110);
		ibpk[1] = nv_rd32(dev, 0x40114);
		
		if (num_fuckups > 10 && num_fuckups < 100) {
			NV_ERROR(dev, "disabled PFIFO FUCKUP interrupt, but still receiving\n");
		}

		// without this, the whole terminal is wiped in seconds
		if (num_fuckups < 10) {
			NV_INFO(dev, "channel %d: PFIFO FUCKUP (SCHED_ERROR): %d(%s) DATA = 0x%08x\n"
				"IB PACKET = 0x%08x 0x%08x\n", chan_id, code, cause, data, ibpk[0], ibpk[1]);
		} else {
			NV_INFO(dev, "too many PFIFO FUCKUPs, disabling interrupts\n");
			nv_wr32(dev, 0x2140, nv_rd32(dev, 0x2140) & ~0x00000100);
		}
		
		status &= ~0x00000100;
	}

	if (status) {
		NV_INFO(dev, "unknown PFIFO INTR: 0x%08x, disabling\n", status);
		/* disable unknown interrupts */
		nv_wr32(dev, 0x2140, nv_rd32(dev, 0x2140) & ~status);
	}
}

/*******************************************************************************
 * PFIFO initialization and takedown
 ******************************************************************************/

int
nvc0_fifo_ctor(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	struct nvc0_fifo_engine *fifo;
	int ret;
	
	fifo = kzalloc(sizeof(*fifo), GFP_KERNEL);
	if (!fifo) {
		NV_ERROR(dev, "PFIFO: could not allocate engine\n");
		ret = -ENOMEM;
		goto fail_kzalloc;
	}
	
	fifo->base.base.dev = dev;
	fifo->base.base.takedown = nvc0_fifo_takedown;
	fifo->base.base.chan_kill = nvc0_fifo_chan_kill;
	fifo->base.base.chan_free = nvc0_fifo_chan_free;
	fifo->base.chan_init_ib = nvc0_fifo_chan_init_ib;
	
	fifo->ctrl_bo = pscnv_mem_alloc(dev, 128 * 0x1000,
			PSCNV_GEM_CONTIG | PSCNV_ZEROFILL | PSCNV_MAP_USER,
			0, 0xf1f03e95, NULL);

	if (!fifo->ctrl_bo) {
		NV_ERROR(dev, "PFIFO: couldn't allocate control area\n");
		ret = -ENOMEM;
		goto fail_ctrl;
	}

	fifo->playlist[0] = pscnv_mem_alloc(dev, 0x1000,
		PSCNV_GEM_CONTIG | PSCNV_MAP_KERNEL,
		0, 0x91a71157, NULL);
	fifo->playlist[1] = pscnv_mem_alloc(dev, 0x1000,
		PSCNV_GEM_CONTIG | PSCNV_MAP_KERNEL,
		0, 0x91a71157, NULL);
	if (!fifo->playlist[0] || !fifo->playlist[1]) {
		NV_ERROR(dev, "PFIFO: Couldn't allocate playlists!\n");
		ret = -ENOMEM;
		goto fail_playlists;
	}
	
	dev_priv->fifo = &fifo->base;
	dev_priv->engines[PSCNV_ENGINE_FIFO] = &fifo->base.base;
	
	return 0;

fail_playlists:
	if (fifo->playlist[0])
		pscnv_mem_free(fifo->playlist[0]);
	if (fifo->playlist[1])
		pscnv_mem_free(fifo->playlist[1]);

fail_ctrl:
	kfree(fifo);
	
fail_kzalloc:
	return ret;
}

int
nvc0_fifo_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fifo_engine *fifo;
	int subfifo_count;
	int i, ret;

	ret = nvc0_fifo_ctor(dev);
	if (ret) {
		NV_ERROR(dev, "PFIFO: initialization failed\n");
		return ret;
	}
	
	fifo = nvc0_fifo_eng(dev_priv->fifo);
	
	/* reset PFIFO, enable all available PSUBFIFO areas */
	nv_mask(dev, 0x000200, 0x00000100, 0x00000000);
	nv_mask(dev, 0x000200, 0x00000100, 0x00000100);
	nv_wr32(dev, 0x000204, 0xffffffff);
	nv_wr32(dev, 0x002204, 0xffffffff);

	subfifo_count = hweight32(nv_rd32(dev, 0x002204));

	/* assign engines to subfifos */
	if (subfifo_count >= 3) {
		nv_wr32(dev, 0x002208, ~(1 << 0)); /* PGRAPH */
		nv_wr32(dev, 0x00220c, ~(1 << 1)); /* PVP */
		nv_wr32(dev, 0x002210, ~(1 << 1)); /* PPP */
		nv_wr32(dev, 0x002214, ~(1 << 1)); /* PBSP */
		nv_wr32(dev, 0x002218, ~(1 << 2)); /* PCE0 (PCOPY0) */
		nv_wr32(dev, 0x00221c, ~(1 << 1)); /* PCE1 (PCOPY1) */
	}

	/* PSUBFIFO[n] */
	for (i = 0; i < subfifo_count; i++) {
		nv_mask(dev, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nv_wr32(dev, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nv_wr32(dev, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTR_EN */
	}

	/* PFIFO.ENABLE */
	nv_mask(dev, 0x002200, 0x00000001, 0x00000001);

	/* PFIFO.POLL_AREA */
	nv_wr32(dev, 0x2254, (1 << 28) | (fifo->ctrl_bo->map1->start >> 12));

	nouveau_irq_register(dev, 8, nvc0_fifo_irq_handler);

	nv_wr32(dev, 0x002a00, 0xffffffff); /* clears PFIFO.INTR bit 30 */
	nv_wr32(dev, 0x002100, 0xffffffff);
	nv_wr32(dev, 0x2140, 0xbfffffff); /* PFIFO_INTR_EN */

	return 0;
}

static void
nvc0_fifo_takedown(struct pscnv_engine *eng)
{
	struct drm_device *dev = eng->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fifo_engine *fifo = nvc0_fifo_eng(fifo_eng(eng));
	
	nv_wr32(dev, 0x2140, 0);
	nouveau_irq_unregister(dev, 8);
	/* XXX */
	pscnv_mem_free(fifo->playlist[0]);
	pscnv_mem_free(fifo->playlist[1]);
	pscnv_mem_free(fifo->ctrl_bo);
	kfree(fifo);
	
	dev_priv->engines[PSCNV_ENGINE_FIFO] = NULL;
	dev_priv->fifo = NULL;
}