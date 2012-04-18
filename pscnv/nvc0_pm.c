/*
 * Copyright 2011 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "nouveau_drv.h"
#include "nouveau_bios.h"
#include "nouveau_pm.h"

#include "nvc0_pdaemon.fuc.h"

/* this should match nvc0_pdaemon_pointers section in nvc0_pdaemon.fuc */
enum fuc_ops {
   fuc_ops_done = 0,
   fuc_ops_mmwrs,
   fuc_ops_mmwr,
   fuc_ops_mmrd,
   fuc_ops_wait_mask_ext,
   fuc_ops_wait_mask_iord,
   fuc_ops_sleep,
   fuc_ops_enter_lock,
   fuc_ops_leave_lock
};

static u32 read_div(struct drm_device *, int, u32, u32);
static u32 read_pll(struct drm_device *, u32);

static u32
read_vco(struct drm_device *dev, u32 dsrc)
{
	u32 ssrc = nv_rd32(dev, dsrc);
	if (!(ssrc & 0x00000100))
		return read_pll(dev, 0x00e800);
	return read_pll(dev, 0x00e820);
}

static u32
read_pll(struct drm_device *dev, u32 pll)
{
	u32 ctrl = nv_rd32(dev, pll + 0);
	u32 coef = nv_rd32(dev, pll + 4);
	u32 P = (coef & 0x003f0000) >> 16;
	u32 N = (coef & 0x0000ff00) >> 8;
	u32 M = (coef & 0x000000ff) >> 0;
	u32 sclk, doff;

	if (!(ctrl & 0x00000001))
		return 0;

	switch (pll & 0xfff000) {
	case 0x00e000:
		sclk = 27000;
		P = 1;
		break;
	case 0x137000:
		doff = (pll - 0x137000) / 0x20;
		sclk = read_div(dev, doff, 0x137120, 0x137140);
		break;
	case 0x132000:
		switch (pll) {
		case 0x132000:
			sclk = read_pll(dev, 0x132020);
			break;
		case 0x132020:
			sclk = read_div(dev, 0, 0x137320, 0x137330);
			break;
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	return sclk * N / M / P;
}

static u32
read_div(struct drm_device *dev, int doff, u32 dsrc, u32 dctl)
{
	u32 ssrc = nv_rd32(dev, dsrc + (doff * 4));
	u32 sctl = nv_rd32(dev, dctl + (doff * 4));

	switch (ssrc & 0x00000003) {
	case 0:
		if ((ssrc & 0x00030000) != 0x00030000)
			return 27000;
		return 108000;
	case 2:
		return 100000;
	case 3:
		if (sctl & 0x80000000) {
			u32 sclk = read_vco(dev, dsrc + (doff * 4));
			u32 sdiv = (sctl & 0x0000003f) + 2;
			return (sclk * 2) / sdiv;
		}

		return read_vco(dev, dsrc + (doff * 4));
	default:
		return 0;
	}
}

static u32
read_mem(struct drm_device *dev)
{
	u32 ssel = nv_rd32(dev, 0x1373f0);
	if (ssel & 0x00000001)
		return read_div(dev, 0, 0x137300, 0x137310);
	return read_pll(dev, 0x132000);
}

static u32
read_clk(struct drm_device *dev, int clk)
{
	u32 sctl = nv_rd32(dev, 0x137250 + (clk * 4));
	u32 ssel = nv_rd32(dev, 0x137100);
	u32 sclk, sdiv;

	if (ssel & (1 << clk)) {
		if (clk < 7)
			sclk = read_pll(dev, 0x137000 + (clk * 0x20));
		else
			sclk = read_pll(dev, 0x1370e0);
		sdiv = ((sctl & 0x00003f00) >> 8) + 2;
	} else {
		sclk = read_div(dev, clk, 0x137160, 0x1371d0);
		sdiv = ((sctl & 0x0000003f) >> 0) + 2;
	}

	if (sctl & 0x80000000)
		return (sclk * 2) / sdiv;
	return sclk;
}

int
nvc0_pm_clocks_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	perflvl->shader = read_clk(dev, 0x00);
	perflvl->core   = perflvl->shader / 2;
	perflvl->memory = read_mem(dev);
	perflvl->rop    = read_clk(dev, 0x01);
	perflvl->hub07  = read_clk(dev, 0x02);
	perflvl->hub06  = read_clk(dev, 0x07);
	perflvl->hub01  = read_clk(dev, 0x08);
	perflvl->copy   = read_clk(dev, 0x09);
	perflvl->daemon = read_clk(dev, 0x0c);
	perflvl->vdec   = read_clk(dev, 0x0e);
	return 0;
}

struct nvc0_pm_clock {
	u32 freq;
	u32 ssel;
	u32 mdiv;
	u32 dsrc;
	u32 ddiv;
	u32 coef;
};

struct nvc0_pm_state {
	struct drm_device *dev;
	struct nouveau_pm_level *perflvl;
	struct nvc0_pm_clock eng[16];
	struct nvc0_pm_clock mem;
	u32 mem_out[0x400];
	u32 mem_pos;
	u32 mem_10f808;
};

static u32
calc_div(struct drm_device *dev, int clk, u32 ref, u32 freq, u32 *ddiv)
{
	u32 div = min((ref * 2) / freq, (u32)65);
	if (div < 2)
		div = 2;

	*ddiv = div - 2;
	return (ref * 2) / div;
}

static u32
calc_src(struct drm_device *dev, int clk, u32 freq, u32 *dsrc, u32 *ddiv)
{
	u32 sclk;

	/* use one of the fixed frequencies if possible */
	*ddiv = 0x00000000;
	switch (freq) {
	case  27000:
	case 108000:
		*dsrc = 0x00000000;
		if (freq == 108000)
			*dsrc |= 0x00030000;
		return freq;
	case 100000:
		*dsrc = 0x00000002;
		return freq;
	default:
		*dsrc = 0x00000003;
		break;
	}

	/* otherwise, calculate the closest divider */
	sclk = read_vco(dev, clk);
	if (clk < 7)
		sclk = calc_div(dev, clk, sclk, freq, ddiv);
	return sclk;
}

static u32
calc_pll(struct drm_device *dev, int clk, u32 freq, u32 *coef)
{
	struct pll_lims limits;
	int N, M, P, ret;

	ret = get_pll_limits(dev, 0x137000 + (clk * 0x20), &limits);
	if (ret)
		return 0;

	limits.refclk = read_div(dev, clk, 0x137120, 0x137140);
	if (!limits.refclk)
		return 0;

	ret = nva3_calc_pll(dev, &limits, freq, &N, NULL, &M, &P);
	if (ret <= 0)
		return 0;

	*coef = (P << 16) | (N << 8) | M;
	return ret;
}

/* A (likely rather simplified and incomplete) view of the clock tree
 *
 * Key:
 *
 * S: source select
 * D: divider
 * P: pll
 * F: switch
 *
 * Engine clocks:
 *
 * 137250(D) ---- 137100(F0) ---- 137160(S)/1371d0(D) ------------------- ref
 *                      (F1) ---- 1370X0(P) ---- 137120(S)/137140(D) ---- ref
 *
 * Not all registers exist for all clocks.  For example: clocks >= 8 don't
 * have their own PLL (all tied to clock 7's PLL when in PLL mode), nor do
 * they have the divider at 1371d0, though the source selection at 137160
 * still exists.  You must use the divider at 137250 for these instead.
 *
 * Memory clock:
 *
 * TBD, read_mem() above is likely very wrong...
 *
 */

static int
calc_clk(struct drm_device *dev, int clk, struct nvc0_pm_clock *info, u32 freq)
{
	u32 src0, div0, div1D, div1P = 0;
	u32 clk0, clk1 = 0;

	/* invalid clock domain */
	if (!freq)
		return 0;

	/* first possible path, using only dividers */
	clk0 = calc_src(dev, clk, freq, &src0, &div0);
	clk0 = calc_div(dev, clk, clk0, freq, &div1D);

	/* see if we can get any closer using PLLs */
	if (clk0 != freq && (0x00004387 & (1 << clk))) {
		if (clk < 7)
			clk1 = calc_pll(dev, clk, freq, &info->coef);
		else
			clk1 = read_pll(dev, 0x1370e0);
		clk1 = calc_div(dev, clk, clk1, freq, &div1P);
	}

	/* select the method which gets closest to target freq */
	if (abs((int)freq - clk0) <= abs((int)freq - clk1)) {
		info->dsrc = src0;
		if (div0) {
			info->ddiv |= 0x80000000;
			info->ddiv |= div0 << 8;
			info->ddiv |= div0;
		}
		if (div1D) {
			info->mdiv |= 0x80000000;
			info->mdiv |= div1D;
		}
		info->ssel = 0;
		info->freq = clk0;
	} else {
		if (div1P) {
			info->mdiv |= 0x80000000;
			info->mdiv |= div1P << 8;
		}
		info->ssel = (1 << clk);
		info->freq = clk1;
	}

	return 0;
}


static u32 link_magic[48] = {
	0x00000000, 0xffffffff, 0x55555555, 0xaaaaaaaa, 0x33333333, 0xcccccccc,
	0xf0f0f0f0, 0x0f0f0f0f, 0x00ff00ff, 0xff00ff00, 0x0000ffff, 0xffff0000,
	0x00000000, 0xffffffff, 0x55555555, 0xaaaaaaaa, 0x33333333, 0xcccccccc,
	0xf0f0f0f0, 0x0f0f0f0f, 0x00ff00ff, 0xff00ff00, 0x0000ffff, 0xffff0000,
	0x00000000, 0xffffffff, 0x55555555, 0xaaaaaaaa, 0x33333333, 0xcccccccc,
	0xf0f0f0f0, 0x0f0f0f0f, 0x00ff00ff, 0xff00ff00, 0x0000ffff, 0xffff0000,
	0x00000000, 0xffffffff, 0x55555555, 0xaaaaaaaa, 0x33333333, 0xcccccccc,
	0xf0f0f0f0, 0x0f0f0f0f, 0x00ff00ff, 0xff00ff00, 0x0000ffff, 0xffff0000
};

static void init_mem(struct drm_device *dev)
{
	int i;
	/* Hammer in the magic values once */
	for (i = 0; i < sizeof(link_magic)/sizeof(*link_magic); ++i) {
		u32 p = link_magic[i] & 0xf;
		p |= p << 8;
		nv_wr32(dev, 0x10f968, i<<8);
		nv_wr32(dev, 0x10f96c, i<<8);
		nv_wr32(dev, 0x10f920, p | 0x100);
		nv_wr32(dev, 0x10f924, p | 0x100);
		nv_wr32(dev, 0x10f918, link_magic[i]);
		nv_wr32(dev, 0x10f91c, link_magic[i]);
		nv_wr32(dev, 0x10f920, p);
		nv_wr32(dev, 0x10f924, p);
		nv_wr32(dev, 0x10f918, link_magic[i]);
		nv_wr32(dev, 0x10f91c, link_magic[i]);
	}

	/* Overwrite PDAEMON with our script, but make sure it's dead first..
	 * This is unsafe if using PWM to drive fan speed
	 */
	nv_mask(dev, 0x200, 0x2000, 0);
	nv_mask(dev, 0x200, 0x2000, 0x2000);

	nv_wr32(dev, 0x10a180, 0x01000000);
	for (i = 0; i < sizeof(nvc0_pdaemon_code)/sizeof(*nvc0_pdaemon_code); ++i) {
		if (i % 64 == 0)
			nv_wr32(dev, 0x10a188, i >> 6);
		nv_wr32(dev, 0x10a184, nvc0_pdaemon_code[i]);
	}
}

static int run_mem(struct drm_device *dev, struct nvc0_pm_state *state)
{
	int i, ret;
	/* Upload our script to D[0x800] onward */
	nv_wr32(dev, 0x10a1c0, 0x01000800);
	for (i = 0; i < state->mem_pos; ++i)
		nv_wr32(dev, 0x10a1c4, state->mem_out[i]);

	/* Clear area around stack pointer (used for debug mode mostly) */
	nv_wr32(dev, 0x10a1c0, 0x010055c0);
	for (i = 0; i < 0x440; i += 4)
		nv_wr32(dev, 0x10a1c4, 0);

	if (1) {
		NV_ERROR(dev, "No! I'm not even going to run the script!");
		NV_ERROR(dev, "Even if neutered with exit.. just in case\n");
		return 0;
	}

	/* Fire! */
	nv_wr32(dev, 0x10a104, 0);
	nv_wr32(dev, 0x10a10c, 0);
	nv_wr32(dev, 0x10a100, 2);

	ret = nv_wait(dev, 0x10a100, 0x10, 0x10);
	/* And kill again */
	nv_mask(dev, 0x200, 0x2000, 0);
	nv_mask(dev, 0x200, 0x2000, 0x2000);
	if (!ret) {
		NV_ERROR(dev, "Reclocking failed! Card may be unstable or gone off the bus entirely!\n");
		return -EINVAL;
	}
	return 0;
}

static int
calc_mem(struct drm_device *dev, struct nvc0_pm_clock *info, u32 freq)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pll_lims pll;
	int N, M, P, ret;
	u32 ctrl;

	if (dev_priv->engine.pm.cur == &dev_priv->engine.pm.boot)
		init_mem(dev);

	/* mclk pll input freq comes from another pll, make sure it's on */
	ctrl = nv_rd32(dev, 0x132020);
	if (!(ctrl & 0x00000001)) {
		/* if not, program it to 567MHz.  nfi where this value comes
		 * from - it looks like it's in the pll limits table for
		 * 132000 but the binary driver ignores all my attempts to
		 * change this value.
		 */
		nv_wr32(dev, 0x137320, 0x00000103);
		nv_wr32(dev, 0x137330, 0x81200606);
		nv_wait(dev, 0x132020, 0x00010000, 0x00010000);
		nv_wr32(dev, 0x132024, 0x0001160f);
		nv_mask(dev, 0x132020, 0x00000001, 0x00000001);
		nv_wait(dev, 0x137390, 0x00020000, 0x00020000);
		nv_mask(dev, 0x132020, 0x00000004, 0x00000004);
	}

	/* for the moment, until the clock tree is better understood, use
	 * pll mode for all clock frequencies
	 */
	ret = get_pll_limits(dev, 0x132000, &pll);
	if (ret == 0) {
		pll.refclk = read_pll(dev, 0x132020);
		if (pll.refclk) {
			ret = nva3_calc_pll(dev, &pll, freq, &N, NULL, &M, &P);
			if (ret > 0) {
				info->coef = (P << 16) | (N << 8) | M;
				info->freq = ret;
				info->ddiv = nv_rd32(dev, 0x137310) | 0x08000000;
				info->ddiv = (info->ddiv & ~0x3f) | ((info->ddiv & 0x3f00)>>8);
				NV_WARN(dev, "freq %u, 132004: %x 137310: %x", ret, info->coef, info->ddiv);
				return 0;
			} else {
				info->freq = calc_src(dev, 0, freq, &info->dsrc, &info->ddiv);
				info->ddiv |= nv_rd32(dev, 0x137310) & 0xf7ffff00;
				NV_WARN(dev, "freq %u 137310: %08x\n", freq, info->ddiv);
				return 0;
			}
		}
	}

	return -EINVAL;
}

void *
nvc0_pm_clocks_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_pm_state *info;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	/* NFI why this is still in the performance table, the ROPCs appear
	 * to get their clock from clock 2 ("hub07", actually hub05 on this
	 * chip, but, anyway...) as well.  nvatiming confirms hub05 and ROP
	 * are always the same freq with the binary driver even when the
	 * performance table says they should differ.
	 */
	if (dev_priv->chipset == 0xd9)
		perflvl->rop = 0;

	if ((ret = calc_clk(dev, 0x00, &info->eng[0x00], perflvl->shader)) ||
	    (ret = calc_clk(dev, 0x01, &info->eng[0x01], perflvl->rop)) ||
	    (ret = calc_clk(dev, 0x02, &info->eng[0x02], perflvl->hub07)) ||
	    (ret = calc_clk(dev, 0x07, &info->eng[0x07], perflvl->hub06)) ||
	    (ret = calc_clk(dev, 0x08, &info->eng[0x08], perflvl->hub01)) ||
	    (ret = calc_clk(dev, 0x09, &info->eng[0x09], perflvl->copy)) ||
	    (ret = calc_clk(dev, 0x0c, &info->eng[0x0c], perflvl->daemon)) ||
	    (ret = calc_clk(dev, 0x0e, &info->eng[0x0e], perflvl->vdec))) {
		kfree(info);
		return ERR_PTR(ret);
	}
	if (perflvl->memory && dev_priv->engine.pm.cur->memory != perflvl->memory && dev_priv->vram_type == NV_MEM_TYPE_GDDR5) {
		ret = calc_mem(dev, &info->mem, perflvl->memory);
		if (ret) {
			kfree(info);
			return ERR_PTR(ret);
		}
	}

	info->perflvl = perflvl;
	info->dev = dev;
	return info;
}

static void
prog_clk(struct drm_device *dev, int clk, struct nvc0_pm_clock *info)
{
	/* program dividers at 137160/1371d0 first */
	if (clk < 7 && !info->ssel) {
		nv_mask(dev, 0x1371d0 + (clk * 0x04), 0x80003f3f, info->ddiv);
		nv_wr32(dev, 0x137160 + (clk * 0x04), info->dsrc);
	}

	/* switch clock to non-pll mode */
	nv_mask(dev, 0x137100, (1 << clk), 0x00000000);
	nv_wait(dev, 0x137100, (1 << clk), 0x00000000);

	/* reprogram pll */
	if (clk < 7) {
		/* make sure it's disabled first... */
		u32 base = 0x137000 + (clk * 0x20);
		u32 ctrl = nv_rd32(dev, base + 0x00);
		if (ctrl & 0x00000001) {
			nv_mask(dev, base + 0x00, 0x00000004, 0x00000000);
			nv_mask(dev, base + 0x00, 0x00000001, 0x00000000);
		}
		/* program it to new values, if necessary */
		if (info->ssel) {
			nv_wr32(dev, base + 0x04, info->coef);
			nv_mask(dev, base + 0x00, 0x00000001, 0x00000001);
			nv_wait(dev, base + 0x00, 0x00020000, 0x00020000);
			nv_mask(dev, base + 0x00, 0x00020004, 0x00000004);
		}
	}

	/* select pll/non-pll mode, and program final clock divider */
	nv_mask(dev, 0x137100, (1 << clk), info->ssel);
	nv_wait(dev, 0x137100, (1 << clk), info->ssel);
	nv_mask(dev, 0x137250 + (clk * 0x04), 0x00003f3f, info->mdiv);
}

static void fuc_emit(struct nvc0_pm_state *info, enum fuc_ops func, u32 len_args, u32 args[], u32 saveret)
{
	u32 j;
	info->mem_out[info->mem_pos++] = (8 + 4 * len_args) | (saveret << 31);
	info->mem_out[info->mem_pos++] = nvc0_pdaemon_pointers[func];
	for (j = 0; j < len_args; ++j)
		info->mem_out[info->mem_pos++] = args[j];

	switch (func) {
		case fuc_ops_mmwrs:
			NV_INFO(info->dev, "S: mmwrs(%#x, %#x)\n", args[0], args[1]);
			break;
		case fuc_ops_mmwr:
			NV_INFO(info->dev, "S: mmwr(%#x, %#x)\n", args[0], args[1]);
			break;
		case fuc_ops_mmrd:
			NV_INFO(info->dev, "S: mmrd(%#x)\n", args[0]);
			break;
		case fuc_ops_wait_mask_ext:
			NV_INFO(info->dev, "S: wait(%#x, %#x, %#x, %u)\n", args[0], args[1], args[2], args[3]);
			break;
		case fuc_ops_wait_mask_iord:
			NV_INFO(info->dev, "S: wait(I[%#x], %#x, %#x, %u)\n", args[0], args[1], args[2], args[3]);
			break;
		case fuc_ops_sleep:
			NV_INFO(info->dev, "S: nsleep(%u)\n", args[0]);
			break;
		case fuc_ops_enter_lock:
			NV_INFO(info->dev, "S: enter_lock()\n");
			break;
		case fuc_ops_leave_lock:
			NV_INFO(info->dev, "S: leave_lock()\n");
			break;
		case fuc_ops_done:
			NV_INFO(info->dev, "S: exit()\n");
			break;
		default:
			NV_ERROR(info->dev, "S: unknown op %i\n", func);
			break;
	}
}

static void fuc_wr32(struct nvc0_pm_state *info, u32 dst, u32 val)
{
	u32 args[2] = { dst, val };
	fuc_emit(info, fuc_ops_mmwr, 2, args, 0);
}

static void fuc_sleep(struct nvc0_pm_state *info, u32 duration)
{
	u32 args[2];
	if (!duration)
		return;
	if (duration < 1000) {
		args[0] = 0x13d974; // XXX: nvc4 uses a different one what about d9?
		args[1] = 0;
		fuc_emit(info, fuc_ops_mmwr, 2, args, 0);
	} else {
		args[0] = 0;
		args[1] = 0;
		fuc_emit(info, fuc_ops_mmrd, 2, args, 1);
	}
	args[0] = duration;
	fuc_emit(info, fuc_ops_sleep, 1, args, 0);
}

static void fuc_wait(struct nvc0_pm_state *info, u32 reg, u32 mask, u32 val, u32 duration)
{
	u32 args[4] = { reg, mask, val, duration };
	fuc_emit(info, fuc_ops_wait_mask_ext, 4, args, 1);
}

static void fuc_enter_lock(struct nvc0_pm_state *info)
{
	struct drm_nouveau_private *dev_priv = info->dev->dev_private;
	u32 args[4] = { 0x1f100, 4, 0, 50000000 };
	fuc_emit(info, fuc_ops_wait_mask_iord, 4, args, 1);
	args[2] = args[1];
	fuc_emit(info, fuc_ops_wait_mask_iord, 4, args, 1);

	if (dev_priv->chipset < 0xd0)
		fuc_wr32(info, 0x611200, 0x00003300);
	else
		fuc_wr32(info, 0x62c000, 0x03030000);
	fuc_emit(info, fuc_ops_enter_lock, 0, 0, 0);
}

static void fuc_leave_lock(struct nvc0_pm_state *info)
{
	struct drm_nouveau_private *dev_priv = info->dev->dev_private;
	if (dev_priv->chipset < 0xd0)
		fuc_wr32(info, 0x611200, 0x00003300);
	else
		fuc_wr32(info, 0x62c000, 0x03030300);
}

static void
mclk_precharge(struct nouveau_mem_exec_func *exec)
{
	fuc_wr32(exec->priv, 0x10f314, 1);
}

static void
mclk_refresh(struct nouveau_mem_exec_func *exec)
{
	fuc_wr32(exec->priv, 0x10f310, 1);
}

static void
mclk_refresh_auto(struct nouveau_mem_exec_func *exec, bool enable)
{
	if (!enable) {
		fuc_wr32(exec->priv, 0x10f200, nv_rd32(exec->dev, 0x10f200) & ~0x800);
		fuc_wr32(exec->priv, 0x10f808, nv_rd32(exec->dev, 0x10f808) & ~0); // XXX: What mask? None noticed..
	}
	fuc_wr32(exec->priv, 0x10f210, enable ? 0x80000000 : 0x00000000);
}

static void
mclk_refresh_self(struct nouveau_mem_exec_func *exec, bool enable)
{
}

static void
mclk_wait(struct nouveau_mem_exec_func *exec, u32 nsec)
{
	fuc_sleep(exec->priv, nsec);
}

static u32
mclk_mrg(struct nouveau_mem_exec_func *exec, int mr)
{
	struct drm_device *dev = exec->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	if (dev_priv->vram_type != NV_MEM_TYPE_GDDR5) {
		if (mr <= 1)
			return nv_rd32(dev, 0x10f300 + ((mr - 0) * 4));
		return nv_rd32(dev, 0x10f320 + ((mr - 2) * 4));
	} else {
		if (mr == 0)
			return nv_rd32(dev, 0x10f300 + (mr * 4));
		else
		if (mr <= 7)
			return nv_rd32(dev, 0x10f32c + (mr * 4));
		return nv_rd32(dev, 0x10f34c);
	}
}

static void
mclk_mrs(struct nouveau_mem_exec_func *exec, int mr, u32 data)
{
	struct drm_device *dev = exec->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	if (dev_priv->vram_type != NV_MEM_TYPE_GDDR5) {
		if (mr <= 1) {
			fuc_wr32(exec->priv, 0x10f300 + ((mr - 0) * 4), data);
			if (dev_priv->vram_rank_B)
				fuc_wr32(exec->priv, 0x10f308 + ((mr - 0) * 4), data);
		} else
		if (mr <= 3) {
			fuc_wr32(exec->priv, 0x10f320 + ((mr - 2) * 4), data);
			if (dev_priv->vram_rank_B)
				fuc_wr32(exec->priv, 0x10f328 + ((mr - 2) * 4), data);
		}
	} else {
		if      (mr ==  0) fuc_wr32(exec->priv, 0x10f300 + (mr * 4), data);
		else if (mr <=  7) fuc_wr32(exec->priv, 0x10f32c + (mr * 4), data);
		else if (mr == 15) fuc_wr32(exec->priv, 0x10f34c, data);
	}
}

static void
mclk_clock_set(struct nouveau_mem_exec_func *exec)
{
	struct nvc0_pm_state *info = exec->priv;
	struct drm_device *dev = exec->dev;
	u32 pll = info->mem.coef;
#if 0
	u32 ctrl = nv_rd32(dev, 0x132000);

	nv_wr32(dev, 0x137360, 0x00000001);
	nv_wr32(dev, 0x137370, 0x00000000);
	nv_wr32(dev, 0x137380, 0x00000000);
	if (ctrl & 0x00000001)
		nv_wr32(dev, 0x132000, (ctrl &= ~0x00000001));

	nv_wr32(dev, 0x132004, info->mem.coef);
	nv_wr32(dev, 0x132000, (ctrl |= 0x00000001));
	nv_wait(dev, 0x137390, 0x00000002, 0x00000002);
	nv_wr32(dev, 0x132018, 0x00005000);

	nv_wr32(dev, 0x137370, 0x00000001);
	nv_wr32(dev, 0x137380, 0x00000001);
	nv_wr32(dev, 0x137360, 0x00000000);
#else
//up: 10f824: 0x7e77 -> 0x7fd4 (setting 0x100, altering low bits
//down: 10f824: 0x7fd4 -> 0x7e54 (old & 0x77) -> 7e77 (setting new bits)

	nv_wr32(dev, 0x137360, 0x00000001);
	fuc_wr32(info, 0x10f090, 0x61);
	fuc_wr32(info, 0x10f090, 0xc000007f);
	fuc_sleep(info, 1000);

	// Bla bla clock stuff
	if (pll) {
		fuc_wr32(info, 0x10f824, (nv_rd32(dev, 0x10f824) & ~0xff) | 0x1d4);
		fuc_wr32(info, 0x10f800, nv_rd32(dev, 0x10f800) & ~0x4);
		fuc_sleep(info, 558);

		fuc_wr32(info, 0x1373ec, 0); // XXX: Generated somehow?
		fuc_wr32(info, 0x1373f0, 3);
		fuc_wr32(info, 0x10f830, 0x40700010);
		fuc_wr32(info, 0x10f830, 0x40500010);
		fuc_sleep(info, 372);
		fuc_wr32(info, 0x1373f8, 0); // & ~0x2000 ??
		fuc_wr32(info, 0x132100, 0x101);
		fuc_wr32(info, 0x137310, 0x89201616); // XXX Generated
		fuc_wr32(info, 0x10f050, 0xff000090);
		fuc_wr32(info, 0x1373ec, 0x30000); // XXX: Generated
		fuc_wr32(info, 0x1373f0, 2);
		fuc_wr32(info, 0x132100, 1);
		fuc_wr32(info, 0x1373f8, 0x2000); // |= 0x2000 again?
		fuc_sleep(info, 2000);

		fuc_wr32(info, 0x10f808, (info->mem_10f808 & ~0x30000000) | (nv_rd32(dev, 0x10f808) & 0x30000000));
		fuc_wr32(info, 0x10f830, 0x500010);
		fuc_wr32(info, 0x10f200, nv_rd32(dev, 0x10f200) & ~0x8800);
	} else {
		fuc_wr32(info, 0x1373ec, 0x20000); // XXX: Generated
		fuc_wr32(info, 0x10f808, nv_rd32(dev, 0x10f808) & ~0x80000);
		fuc_wr32(info, 0x10f200, nv_rd32(dev, 0x10f200) & ~0x8800);
		fuc_wr32(info, 0x10f830, 0x41500010);
		fuc_wr32(info, 0x10f830, 0x40500010);
		fuc_sleep(info, 50);

		fuc_wr32(info, 0x132100, 0x101);
		fuc_wr32(info, 0x137310, 0x89201608);
		fuc_wr32(info, 0x10f050, 0xff000090);
		fuc_wr32(info, 0x1373ec, 0x20f0f);
		fuc_wr32(info, 0x1373f0, 3);
		fuc_wr32(info, 0x137310, 0x81201608);
		fuc_wr32(info, 0x132100, 1);

		fuc_wr32(info, 0x10f830, 0x300017);
		fuc_sleep(info, 100);
		fuc_wr32(info, 0x10f824, 0x7e77);
		fuc_sleep(info, 100);
		fuc_wr32(info, 0x132000, nv_rd32(dev, 0x132000) & ~0); // TODO
		//fuc_wr32(info, 0x10f808, (info->mem_10f808 & ~0x30000000), nv_rd32(dev, 0x10f808) & 0x30000000);
	}
	fuc_wr32(info, 0x10f090, 0x4000007e);
	fuc_sleep(info, 2000);
#endif
}

static void
mclk_timing_set(struct nouveau_mem_exec_func *exec)
{
	struct drm_device *dev = exec->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_pm_state *info = exec->priv;
	struct nouveau_pm_level *perflvl = info->perflvl;
	int i;
	int pll = info->mem.coef;
	u32 reg;

	for (i = 0; i < 5; i++) {
		u32 val = perflvl->timing.reg[i];
		if (i == 3 && pll)
			val &= ~0xff;
		fuc_wr32(info, 0x10f290 + (i * 4), val);
	}
	if (!dev_priv->engine.pm.boot.timing.etc[0])
		dev_priv->engine.pm.boot.timing.etc[0] = nv_rd32(dev, 0x10f604);
	if (pll) {
		reg = nv_rd32(dev, 0x10f604);
		if (!(reg & 0x01000000))
			fuc_wr32(info, 0x10f604, reg | 0x01000000);
		fuc_wr32(info, 0x10f614, (nv_rd32(dev, 0x10f614) & ~0x100) | 0x20000000);
		fuc_wr32(info, 0x10f610, (nv_rd32(dev, 0x10f610) & ~0x100) | 0x20000000);
	} else {
		u32 reg2 = nv_rd32(dev, 0x10f604);
		reg = dev_priv->engine.pm.boot.timing.etc[0];
	        if (reg != reg2)
			fuc_wr32(info, 0x10f604, reg);
		fuc_wr32(info, 0x10f614, (nv_rd32(dev, 0x10f614) & ~0x20000000) | 0x100);
		fuc_wr32(info, 0x10f610, (nv_rd32(dev, 0x10f610) & ~0x20000000) | 0x100);
	}

	fuc_wr32(info, 0x10f808, info->mem_10f808);
	for (i = 5; i < 9; ++i)
		if (perflvl->timing.reg[i] != nv_rd32(dev, 0x10f32c + (i * 4)))
			fuc_wr32(info, 0x10f32c + (i * 4), perflvl->timing.reg[i]);
}

static void
prog_mem(struct drm_device *dev, struct nvc0_pm_state *info)
{
	struct nouveau_mem_exec_func exec = {
		.dev = dev,
		.precharge = mclk_precharge,
		.refresh = mclk_refresh,
		.refresh_auto = mclk_refresh_auto,
		.refresh_self = mclk_refresh_self,
		.wait = mclk_wait,
		.mrg = mclk_mrg,
		.mrs = mclk_mrs,
		.clock_set = mclk_clock_set,
		.timing_set = mclk_timing_set,
		.priv = info
	};
	int i, pll = info->mem.coef, mcs = nv_rd32(dev, 0x121c74);

	/* Not yet, do not even TRY to run what we generate.. */
	fuc_emit(info, fuc_ops_done, 0, 0, 0);

	if (pll) {
		fuc_wr32(info, 0x10fb04, 0x55550000);
		fuc_wr32(info, 0x10fb08, 0x55550000);
		// Why do you look like a PLL so much?
		fuc_wr32(info, 0x10f988, 0x2004ff00);
		fuc_wr32(info, 0x10f98c, 0x3fc040);
		fuc_wr32(info, 0x10f990, 0x20012001);
		fuc_wr32(info, 0x10f998, 0x11a00);
		fuc_sleep(info, 1000);
	} else {
		fuc_wr32(info, 0x10f988, 0x20010000);
		fuc_wr32(info, 0x10f98c, 0);
		fuc_wr32(info, 0x10f990, 0x20012001);
		fuc_wr32(info, 0x10f998, 0x11a00);
	}

	fuc_wr32(info, 0x100b0c, (nv_rd32(dev, 0x100b0c) & ~0xff) | 0x12);
	fuc_enter_lock(info);
	nouveau_mem_exec(&exec, info->perflvl);
	if (pll) {
		fuc_wr32(info, 0x10f910, 0x800e1008);
		fuc_wr32(info, 0x10f914, 0x800e1008);
		for (i = 0; i < mcs; ++i)
			fuc_wait(info, 0x110974 + i * 0x1000, 0xf, 0, 500000);
		fuc_wr32(info, 0x10f800, nv_rd32(info->dev, 0x10f800));
	} else {
		fuc_wr32(info, 0x10f910, 0x80021001);
		fuc_wr32(info, 0x10f914, 0x80021001);
		for (i = 0; i < mcs; ++i)
			fuc_wait(info, 0x110974 + i * 0x1000, 0xf, 0, 500000);
		fuc_wr32(info, 0x10f910, 0x80081001);
		fuc_wr32(info, 0x10f914, 0x80081001);
		for (i = 0; i < mcs; ++i)
			fuc_wait(info, 0x110974 + i * 0x1000, 0xf, 0, 500000);
	}
	fuc_leave_lock(info);
	if (pll) {
		fuc_sleep(info, 100000);
		fuc_wr32(info, 0x10f9b0, 0x5313f41);
		fuc_wr32(info, 0x10f9b4, 0x2f50);
		fuc_wr32(info, 0x10f910, 0x10c1001);
		fuc_wr32(info, 0x10f914, 0x10c1001);
	}
	fuc_wr32(info, 0x100b0c, nv_rd32(dev, 0x100b0c));
	fuc_emit(info, fuc_ops_done, 0, 0, 0);
	run_mem(dev, info);
	nv_mask(dev, 0x10f200, 0x800, 0x800);
}

int
nvc0_pm_clocks_set(struct drm_device *dev, void *data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_pm_state *info = data;
	int i;

	if (info->mem.freq && dev_priv->vram_type == NV_MEM_TYPE_GDDR5)
		prog_mem(dev, info);

	for (i = 0; i < 16; i++) {
		if (!info->eng[i].freq)
			continue;
		prog_clk(dev, i, &info->eng[i]);
	}

	kfree(info);
	return 0;
}
