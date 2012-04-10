/*
 * Copyright 2009 Red Hat Inc.
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
 */

#ifndef __NOUVEAU_I2C_H__
#define __NOUVEAU_I2C_H__

#ifdef __linux__
#include <linux/i2c.h>
#ifdef PSCNV_KAPI_I2C_ID
#include <linux/i2c-id.h>
#else
#include <linux/i2c-dev.h>
#endif
#include <linux/i2c-algo-bit.h>

#else /* __linux __ */

struct i2c_algo_bit_data {
	int (*getsda)(void *data);
	int (*getscl)(void *data);
	void (*setsda)(void *data, int sda);
	void (*setscl)(void *data, int sdl);
};
struct i2c_board_info { const char *name; int addr; };
#define I2C_BOARD_INFO(a, b) (a), (b)
#define I2C_M_RD IIC_M_RD
#define i2c_msg iic_msg

#include <dev/iicbus/iic.h>
#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include "iicbus_if.h"
#include "iicbb_if.h"

static inline int i2c_transfer(device_t *dev, struct iic_msg *msg, int n) {
	int ret;
	ret = IICBUS_TRANSFER(*dev, msg, n);
	return ret ? -ret : n;
}

#endif

struct nouveau_i2c_chan {
#ifdef __linux__
	struct i2c_adapter adapter;
#else
	device_t adapter, bus, iic_dev;
	char name[32];
#endif
	struct drm_device *dev;
	struct i2c_algo_bit_data bit;
	unsigned rd;
	unsigned wr;
	unsigned data;
};

#include "drm_dp_helper.h"

struct dcb_i2c_entry;
int nouveau_i2c_init(struct drm_device *, struct dcb_i2c_entry *, int index);
void nouveau_i2c_fini(struct drm_device *, struct dcb_i2c_entry *);
struct nouveau_i2c_chan *nouveau_i2c_find(struct drm_device *, int index);
bool nouveau_probe_i2c_addr(struct nouveau_i2c_chan *i2c, int addr);
int nouveau_i2c_identify(struct drm_device *dev, const char *what,
			 struct i2c_board_info *info,
			 bool (*match)(struct nouveau_i2c_chan *,
				       struct i2c_board_info *),
			 int index);

extern const struct i2c_algorithm nouveau_dp_i2c_algo;

#endif /* __NOUVEAU_I2C_H__ */
