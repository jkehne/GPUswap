/*
 * Copyright (C) 2009 Francisco Jerez.
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

#ifndef __DRM_ENCODER_SLAVE_H__
#define __DRM_ENCODER_SLAVE_H__

#include "drmP.h"
#include "drm_mode.h"
#include "drm_crtc.h"

/**
 * struct drm_encoder_slave_funcs - Entry points exposed by a slave encoder driver
 * @set_config:	Initialize any encoder-specific modesetting parameters.
 *		The meaning of the @params parameter is implementation
 *		dependent. It will usually be a structure with DVO port
 *		data format settings or timings. It's not required for
 *		the new parameters to take effect until the next mode
 *		is set.
 *
 * Most of its members are analogous to the function pointers in
 * &drm_encoder_helper_funcs and they can optionally be used to
 * initialize the latter. Connector-like methods (e.g. @get_modes and
 * @set_property) will typically be wrapped around and only be called
 * if the encoder is the currently selected one for the connector.
 */
struct drm_encoder_slave_funcs {
	void (*set_config)(struct drm_encoder *encoder,
			   void *params);

	void (*destroy)(struct drm_encoder *encoder);
	void (*dpms)(struct drm_encoder *encoder, int mode);
	void (*save)(struct drm_encoder *encoder);
	void (*restore)(struct drm_encoder *encoder);
	bool (*mode_fixup)(struct drm_encoder *encoder,
			   struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	int (*mode_valid)(struct drm_encoder *encoder,
			  struct drm_display_mode *mode);
	void (*mode_set)(struct drm_encoder *encoder,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode);

	enum drm_connector_status (*detect)(struct drm_encoder *encoder,
					    struct drm_connector *connector);
	int (*get_modes)(struct drm_encoder *encoder,
			 struct drm_connector *connector);
	int (*create_resources)(struct drm_encoder *encoder,
				 struct drm_connector *connector);
	int (*set_property)(struct drm_encoder *encoder,
			    struct drm_connector *connector,
			    struct drm_property *property,
			    uint64_t val);

};

/**
 * struct drm_encoder_slave - Slave encoder struct
 * @base: DRM encoder object.
 * @slave_funcs: Slave encoder callbacks.
 * @slave_priv: Slave encoder private data.
 * @bus_priv: Bus specific data.
 *
 * A &drm_encoder_slave has two sets of callbacks, @slave_funcs and the
 * ones in @base. The former are never actually called by the common
 * CRTC code, it's just a convenience for splitting the encoder
 * functions in an upper, GPU-specific layer and a (hopefully)
 * GPU-agnostic lower layer: It's the GPU driver responsibility to
 * call the slave methods when appropriate.
 *
 * drm_i2c_encoder_init() provides a way to get an implementation of
 * this.
 */
struct drm_encoder_slave {
	struct drm_encoder base;

	struct drm_encoder_slave_funcs *slave_funcs;
	void *slave_priv;
	void *bus_priv;
};
#define to_encoder_slave(x) container_of((x), struct drm_encoder_slave, base)

#endif
