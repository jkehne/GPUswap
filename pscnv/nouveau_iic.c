/*
 * Copyright 2009 Red Hat Inc.
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2008,2010 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include "nouveau_i2c.h"
#include "nouveau_hw.h"

static void
nv04_i2c_setscl(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;
	uint8_t val;

	val = (NVReadVgaCrtc(dev, 0, i2c->wr) & 0xd0) | (state ? 0x20 : 0);
	NVWriteVgaCrtc(dev, 0, i2c->wr, val | 0x01);
}

static void
nv04_i2c_setsda(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;
	uint8_t val;

	val = (NVReadVgaCrtc(dev, 0, i2c->wr) & 0xe0) | (state ? 0x10 : 0);
	NVWriteVgaCrtc(dev, 0, i2c->wr, val | 0x01);
}

static int
nv04_i2c_getscl(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!(NVReadVgaCrtc(dev, 0, i2c->rd) & 4);
}

static int
nv04_i2c_getsda(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!(NVReadVgaCrtc(dev, 0, i2c->rd) & 8);
}

static void
nv4e_i2c_setscl(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;
	uint8_t val;

	val = (nv_rd32(dev, i2c->wr) & 0xd0) | (state ? 0x20 : 0);
	nv_wr32(dev, i2c->wr, val | 0x01);
}

static void
nv4e_i2c_setsda(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;
	uint8_t val;

	val = (nv_rd32(dev, i2c->wr) & 0xe0) | (state ? 0x10 : 0);
	nv_wr32(dev, i2c->wr, val | 0x01);
}

static int
nv4e_i2c_getscl(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!((nv_rd32(dev, i2c->rd) >> 16) & 4);
}

static int
nv4e_i2c_getsda(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!((nv_rd32(dev, i2c->rd) >> 16) & 8);
}

static int
nv50_i2c_getscl(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!(nv_rd32(dev, i2c->rd) & 1);
}


static int
nv50_i2c_getsda(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!(nv_rd32(dev, i2c->rd) & 2);
}

static void
nv50_i2c_setscl(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	nv_wr32(dev, i2c->wr, 4 | (i2c->data ? 2 : 0) | (state ? 1 : 0));
}

static void
nv50_i2c_setsda(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	nv_wr32(dev, i2c->wr,
			(nv_rd32(dev, i2c->rd) & 1) | 4 | (state ? 2 : 0));
	i2c->data = state;
}

static int
nvd0_i2c_getscl(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	return !!(nv_rd32(i2c->dev, i2c->rd) & 0x10);
}

static int
nvd0_i2c_getsda(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	return !!(nv_rd32(i2c->dev, i2c->rd) & 0x20);
}

static void
pscnv_iicbb_setsda(device_t idev, int val)
{
	struct nouveau_i2c_chan *i2c = device_get_softc(idev);
	i2c->bit.setsda(i2c, val);
}

static void
pscnv_iicbb_setscl(device_t idev, int val)
{
	struct nouveau_i2c_chan *i2c = device_get_softc(idev);
	i2c->bit.setscl(i2c, val);
}

static int
pscnv_iicbb_getsda(device_t idev)
{
	struct nouveau_i2c_chan *i2c = device_get_softc(idev);
	return i2c->bit.getsda(i2c);
}

static int
pscnv_iicbb_getscl(device_t idev)
{
	struct nouveau_i2c_chan *i2c = device_get_softc(idev);
	return i2c->bit.getscl(i2c);
}


static const uint32_t nv50_i2c_port[] = {
	0x00e138, 0x00e150, 0x00e168, 0x00e180,
	0x00e254, 0x00e274, 0x00e764, 0x00e780,
	0x00e79c, 0x00e7b8
};
#define NV50_I2C_PORTS DRM_ARRAY_SIZE(nv50_i2c_port)

int
nouveau_i2c_init(struct drm_device *dev, struct dcb_i2c_entry *entry, int index)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_i2c_chan *i2c;
	int ret;
	device_t idev;

	if (entry->chan)
		return -EEXIST;

	if (dev_priv->card_type >= NV_50 && entry->read >= NV50_I2C_PORTS) {
		NV_ERROR(dev, "unknown i2c port %d\n", entry->read);
		return -EINVAL;
	}

	if (entry->port_type < 6) {
		idev = device_add_child(dev->device, "pscnv_iicbb", index);
	} else {
		idev = device_add_child(dev->device, "pscnv_gmbus", index);
	}
	if (!idev) {
		return -ENODEV;
	}
	ret = device_probe_and_attach(idev);
	if (ret) {
		NV_ERROR(dev, "Couldn't attach device: %d\n", ret);
		device_delete_child(dev->device, idev);
		return -ret;
	}
	device_quiet(idev);
	i2c = device_get_softc(idev);
	if (!i2c) {
		NV_ERROR(dev, "Erp?!\n");
		return -ENODEV;
	}

	switch (entry->port_type) {
	case 0:
		i2c->bit.setsda = nv04_i2c_setsda;
		i2c->bit.setscl = nv04_i2c_setscl;
		i2c->bit.getsda = nv04_i2c_getsda;
		i2c->bit.getscl = nv04_i2c_getscl;
		i2c->rd = entry->read;
		i2c->wr = entry->write;
		break;
	case 4:
		i2c->bit.setsda = nv4e_i2c_setsda;
		i2c->bit.setscl = nv4e_i2c_setscl;
		i2c->bit.getsda = nv4e_i2c_getsda;
		i2c->bit.getscl = nv4e_i2c_getscl;
		i2c->rd = 0x600800 + entry->read;
		i2c->wr = 0x600800 + entry->write;
		break;
	case 5:
		i2c->bit.setsda = nv50_i2c_setsda;
		i2c->bit.setscl = nv50_i2c_setscl;
		if (dev_priv->card_type < NV_D0) {
			i2c->bit.getsda = nv50_i2c_getsda;
			i2c->bit.getscl = nv50_i2c_getscl;
			i2c->rd = nv50_i2c_port[entry->read];
		} else {
			i2c->bit.getsda = nvd0_i2c_getsda;
			i2c->bit.getscl = nvd0_i2c_getscl;
			i2c->rd = 0x00d014 + (entry->read * 0x20);
		}
		i2c->wr = i2c->rd;
		break;
	case 6:
		i2c->rd = entry->read;
		i2c->wr = entry->write;
		break;
	default:
		NV_ERROR(dev, "DCB I2C port type %d unknown\n",
			 entry->port_type);
		return -EINVAL;
	}

	entry->chan = i2c;
	return 0;
}

void
nouveau_i2c_fini(struct drm_device *dev, struct dcb_i2c_entry *entry)
{
	if (!entry->chan)
		return;

	device_delete_child(dev->device, entry->chan->adapter);
}

struct nouveau_i2c_chan *
nouveau_i2c_find(struct drm_device *dev, int index)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct dcb_i2c_entry *i2c = &dev_priv->vbios.dcb.i2c[index];

	if (index >= DCB_MAX_NUM_I2C_ENTRIES)
		return NULL;

	if (dev_priv->chipset >= NV_50 && (i2c->entry & 0x00000100)) {
		uint32_t reg = 0xe500, val;

		if (i2c->port_type == 6) {
			reg += i2c->read * 0x50;
			val  = 0x2002;
		} else {
			reg += ((i2c->entry & 0x1e00) >> 9) * 0x50;
			val  = 0xe001;
		}

		nv_wr32(dev, reg, (nv_rd32(dev, reg) & ~0xf003) | val);
	}

	if (!i2c->chan && nouveau_i2c_init(dev, i2c, index))
		return NULL;
	return i2c->chan;
}

bool
nouveau_probe_i2c_addr(struct nouveau_i2c_chan *i2c, int addr)
{
	uint8_t buf[] = { 0 };
	struct i2c_msg msgs[] = {
		{
			.slave = addr,
			.flags = 0,
			.len = 1,
			.buf = buf,
		},
		{
			.slave = addr,
			.flags = IIC_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	return !iicbus_transfer(i2c->bus, msgs, 2);
}

int
nouveau_i2c_identify(struct drm_device *dev, const char *what,
		     struct i2c_board_info *info,
		     bool (*match)(struct nouveau_i2c_chan *,
				   struct i2c_board_info *),
		     int index)
{
	struct nouveau_i2c_chan *i2c = nouveau_i2c_find(dev, index);
	int i;

	NV_DEBUG(dev, "Probing %ss on I2C bus: %d\n", what, index);

	for (i = 0; info[i].addr; i++) {
		if (nouveau_probe_i2c_addr(i2c, info[i].addr) &&
		    (!match || match(i2c, &info[i]))) {
			NV_INFO(dev, "Detected %s on %i\n", what, index);
			return i;
		}
	}

	NV_DEBUG(dev, "No devices found.\n");
	return -ENODEV;
}

static int
pscnv_gmbus_attach(device_t idev)
{
	struct drm_nouveau_private *dev_priv;
	struct nouveau_i2c_chan *sc;
	int pin;

	sc = device_get_softc(idev);
	sc->dev = device_get_softc(device_get_parent(idev));
	dev_priv = sc->dev->dev_private;
	pin = device_get_unit(idev);

	snprintf(sc->name, sizeof(sc->name), "pscnv_iicbb %u", pin);
	device_set_desc(idev, sc->name);

	/* add bus interface device */
	sc->bus = sc->iic_dev = device_add_child(idev, "iicbus", -1);
	if (sc->iic_dev == NULL) {
		NV_ERROR(sc->dev, "Could not add iicbus to gmbus!\n");
		return (ENXIO);
	}
	device_quiet(sc->iic_dev);
	bus_generic_attach(idev);

	return (0);
}

static int
pscnv_iicbb_attach(device_t idev)
{
	struct drm_nouveau_private *dev_priv;
	struct nouveau_i2c_chan *sc;
	int pin;

	sc = device_get_softc(idev);
	sc->dev = device_get_softc(device_get_parent(idev));
	dev_priv = sc->dev->dev_private;
	pin = device_get_unit(idev);

	snprintf(sc->name, sizeof(sc->name), "pscnv_iicbb %u", pin);
	device_set_desc(idev, sc->name);

	/* add bus interface device */
	sc->iic_dev = device_add_child(idev, "iicbb", -1);
	if (sc->iic_dev == NULL) {
		NV_ERROR(sc->dev, "Could not add iicbb to our bitbanger!\n");
		return (ENXIO);
	}
	device_quiet(sc->iic_dev);
	bus_generic_attach(idev);
	sc->bus = device_find_child(sc->iic_dev, "iicbus", -1);

	return (0);
}

static int
pscnv_gmbus_transfer(device_t idev, struct iic_msg *msgs, uint32_t nmsgs)
{
	struct drm_nouveau_private *dev_priv;
	struct nouveau_i2c_chan *auxch;
	struct i2c_msg *msg = msgs;
	int ret, mcnt = nmsgs;

	auxch = device_get_softc(idev);
	dev_priv = auxch->dev->dev_private;

	while (mcnt--) {
		u8 remaining = msg->len;
		u8 *ptr = msg->buf;

		while (remaining) {
			u8 cnt = (remaining > 16) ? 16 : remaining;
			u8 cmd;

			if (msg->flags & I2C_M_RD)
				cmd = AUX_I2C_READ;
			else
				cmd = AUX_I2C_WRITE;

			if (mcnt || remaining > 16)
				cmd |= AUX_I2C_MOT;

			ret = nouveau_dp_auxch(auxch, cmd, msg->slave, ptr, cnt);
			if (ret < 0)
				return (-ret);

			switch (ret & NV50_AUXCH_STAT_REPLY_I2C) {
			case NV50_AUXCH_STAT_REPLY_I2C_ACK:
				break;
			case NV50_AUXCH_STAT_REPLY_I2C_NACK:
				return (EREMOTEIO);
			case NV50_AUXCH_STAT_REPLY_I2C_DEFER:
				udelay(100);
				continue;
			default:
				NV_ERROR(auxch->dev, "bad auxch reply: 0x%08x\n", ret);
				return (EREMOTEIO);
			}

			ptr += cnt;
			remaining -= cnt;
		}

		msg++;
	}

	return (0);
}

static int
pscnv_iic_probe(device_t dev)
{
	return (BUS_PROBE_SPECIFIC);
}

static int
pscnv_iic_detach(device_t idev)
{
	struct nouveau_i2c_chan *sc;
	device_t child;

	sc = device_get_softc(idev);
	child = sc->iic_dev;
	bus_generic_detach(idev);
	if (child)
		device_delete_child(idev, child);
	return (0);
}

static int
pscnv_iicbus_reset(device_t idev, u_char speed, u_char addr, u_char *oldaddr)
{
	return (0);
}

/* DP transfer with auxch */
static device_method_t pscnv_gmbus_methods[] = {
	DEVMETHOD(device_probe,		pscnv_iic_probe),
	DEVMETHOD(device_attach,	pscnv_gmbus_attach),
	DEVMETHOD(device_detach,	pscnv_iic_detach),
	DEVMETHOD(iicbus_reset,		pscnv_iicbus_reset),
	DEVMETHOD(iicbus_transfer,	pscnv_gmbus_transfer),
	DEVMETHOD_END
};
static driver_t pscnv_gmbus_driver = {
	"pscnv_gmbus",
	pscnv_gmbus_methods,
	sizeof(struct nouveau_i2c_chan)
};
static devclass_t pscnv_gmbus_devclass;
DRIVER_MODULE_ORDERED(pscnv_gmbus, drm, pscnv_gmbus_driver,
    pscnv_gmbus_devclass, 0, 0, SI_ORDER_FIRST);
DRIVER_MODULE(iicbus, pscnv_gmbus, iicbus_driver, iicbus_devclass, 0, 0);

/* Bit banging */
static device_method_t pscnv_iicbb_methods[] =	{
	DEVMETHOD(device_probe,		pscnv_iic_probe),
	DEVMETHOD(device_attach,	pscnv_iicbb_attach),
	DEVMETHOD(device_detach,	pscnv_iic_detach),

	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_print_child,	bus_generic_print_child),

	DEVMETHOD(iicbb_callback,	iicbus_null_callback),
	DEVMETHOD(iicbus_reset,		pscnv_iicbus_reset),
	DEVMETHOD(iicbb_setsda,		pscnv_iicbb_setsda),
	DEVMETHOD(iicbb_setscl,		pscnv_iicbb_setscl),
	DEVMETHOD(iicbb_getsda,		pscnv_iicbb_getsda),
	DEVMETHOD(iicbb_getscl,		pscnv_iicbb_getscl),
	DEVMETHOD_END
};
static driver_t pscnv_iicbb_driver = {
	"pscnv_iicbb",
	pscnv_iicbb_methods,
	sizeof(struct nouveau_i2c_chan)
};
static devclass_t pscnv_iicbb_devclass;
DRIVER_MODULE_ORDERED(pscnv_iicbb, drm, pscnv_iicbb_driver,
    pscnv_iicbb_devclass, 0, 0, SI_ORDER_FIRST);
DRIVER_MODULE(iicbb, pscnv_iicbb, iicbb_driver, iicbb_devclass, 0, 0);
