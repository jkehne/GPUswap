/*
 * Copyright (C) 2008 Maarten Maathuis.
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

#include "drmP.h"
#include "drm_crtc_helper.h"
#include "drm_crtc.h"
#include "nouveau_drv.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nouveau_connector.h"
#include "pscnv_kapi.h"

static void
nouveau_user_framebuffer_destroy(struct drm_framebuffer *drm_fb)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);

	if (fb->nvbo && fb->nvbo->gem)
		drm_gem_object_unreference_unlocked(fb->nvbo->gem);

	drm_framebuffer_cleanup(drm_fb);
	kfree(fb);
}

static int
nouveau_user_framebuffer_create_handle(struct drm_framebuffer *drm_fb,
				       struct drm_file *file_priv,
				       unsigned int *handle)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);

	return drm_gem_handle_create(file_priv, fb->nvbo->gem, handle);
}

static const struct drm_framebuffer_funcs nouveau_framebuffer_funcs = {
	.destroy = nouveau_user_framebuffer_destroy,
	.create_handle = nouveau_user_framebuffer_create_handle,
};

#ifdef PSCNV_KAPI_DRM_MODE_FB_CMD2
int
nouveau_framebuffer_init(struct drm_device *dev, struct nouveau_framebuffer *nouveau_fb,
			 struct drm_mode_fb_cmd2 *mode_cmd, struct pscnv_bo *nvbo)
#else
int
nouveau_framebuffer_init(struct drm_device *dev, struct nouveau_framebuffer *nouveau_fb,
			 struct drm_mode_fb_cmd *mode_cmd, struct pscnv_bo *nvbo)
#endif
{
	int ret;

	ret = drm_framebuffer_init(dev, &nouveau_fb->base, &nouveau_framebuffer_funcs);
	if (ret) {
		return ret;
	}

	drm_helper_mode_fill_fb_struct(&nouveau_fb->base, mode_cmd);
	nouveau_fb->nvbo = nvbo;
	return 0;
}

#ifdef PSCNV_KAPI_DRM_MODE_FB_CMD2
static struct drm_framebuffer *
nouveau_user_framebuffer_create(struct drm_device *dev,
				struct drm_file *file_priv,
				struct drm_mode_fb_cmd2 *mode_cmd)
#else
static struct drm_framebuffer *
nouveau_user_framebuffer_create(struct drm_device *dev,
				struct drm_file *file_priv,
				struct drm_mode_fb_cmd *mode_cmd)
#endif
{
	struct nouveau_framebuffer *nouveau_fb;
	struct drm_gem_object *gem;
	int ret;

#ifdef PSCNV_KAPI_DRM_MODE_FB_CMD2
	gem = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);
#else
	gem = drm_gem_object_lookup(dev, file_priv, mode_cmd->handle);
#endif
	if (!gem)
		return ERR_PTR(-ENOENT);

	nouveau_fb = kzalloc(sizeof(struct nouveau_framebuffer), GFP_KERNEL);
	if (!nouveau_fb)
		return ERR_PTR(-ENOMEM);

	ret = nouveau_framebuffer_init(dev, nouveau_fb, mode_cmd, gem->driver_private);
	if (ret) {
		drm_gem_object_unreference(gem);
		return ERR_PTR(ret);
	}

	return &nouveau_fb->base;
}

const struct drm_mode_config_funcs nouveau_mode_config_funcs = {
	.fb_create = nouveau_user_framebuffer_create,
	.output_poll_changed = nouveau_fbcon_output_poll_changed,
};

struct nouveau_drm_prop_enum_list {
	u8 gen_mask;
	int type;
	char *name;
};

static struct nouveau_drm_prop_enum_list underscan[] = {
	{ 6, UNDERSCAN_AUTO, "auto" },
	{ 6, UNDERSCAN_OFF, "off" },
	{ 6, UNDERSCAN_ON, "on" },
	{}
};

static struct nouveau_drm_prop_enum_list dither_mode[] = {
	{ 7, DITHERING_MODE_AUTO, "auto" },
	{ 7, DITHERING_MODE_OFF, "off" },
	{ 1, DITHERING_MODE_ON, "on" },
	{ 6, DITHERING_MODE_STATIC2X2, "static 2x2" },
	{ 6, DITHERING_MODE_DYNAMIC2X2, "dynamic 2x2" },
	{ 4, DITHERING_MODE_TEMPORAL, "temporal" },
	{}
};

static struct nouveau_drm_prop_enum_list dither_depth[] = {
	{ 6, DITHERING_DEPTH_AUTO, "auto" },
	{ 6, DITHERING_DEPTH_6BPC, "6 bpc" },
	{ 6, DITHERING_DEPTH_8BPC, "8 bpc" },
	{}
};

#define PROP_ENUM(p,gen,n,list) do {                                           \
	struct nouveau_drm_prop_enum_list *l = (list);                         \
	int c = 0;                                                             \
	while (l->gen_mask) {                                                  \
		if (l->gen_mask & (1 << (gen)))                                \
			c++;                                                   \
		l++;                                                           \
	}                                                                      \
	if (c) {                                                               \
		p = drm_property_create(dev, DRM_MODE_PROP_ENUM, n, c);        \
		l = (list);                                                    \
		c = 0;                                                         \
		while (p && l->gen_mask) {                                     \
			if (l->gen_mask & (1 << (gen))) {                      \
				drm_property_add_enum(p, c, l->type, l->name); \
				c++;                                           \
			}                                                      \
			l++;                                                   \
		}                                                              \
	}                                                                      \
} while(0)

#if 1 // TODO: Add KAPI test
  struct drm_property *drm_property_create_range(struct drm_device *dev, int flags,
						 const char *name,
						 uint64_t min, uint64_t max)
{
	struct drm_property *property;

	flags |= DRM_MODE_PROP_RANGE;

	property = drm_property_create(dev, flags, name, 2);
	if (!property)
		return NULL;

	property->values[0] = min;
	property->values[1] = max;

	return property;
}
#endif

int
nouveau_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_display_engine *disp = &dev_priv->engine.display;
	int ret, gen;

	drm_mode_config_init(dev);
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dvi_i_properties(dev);

	if (dev_priv->card_type < NV_50)
		gen = 0;
	else
	if (dev_priv->card_type < NV_D0)
		gen = 1;
	else
		gen = 2;

	PROP_ENUM(disp->dithering_mode, gen, "dithering mode", dither_mode);
	PROP_ENUM(disp->dithering_depth, gen, "dithering depth", dither_depth);
	PROP_ENUM(disp->underscan_property, gen, "underscan", underscan);

	disp->underscan_hborder_property =
		drm_property_create_range(dev, 0, "underscan hborder", 0, 128);

	disp->underscan_vborder_property =
		drm_property_create_range(dev, 0, "underscan vborder", 0, 128);

	if (gen == 1) {
		disp->vibrant_hue_property =
			drm_property_create(dev, DRM_MODE_PROP_RANGE,
					    "vibrant hue", 2);
		disp->vibrant_hue_property->values[0] = 0;
		disp->vibrant_hue_property->values[1] = 180; /* -90..+90 */

		disp->color_vibrance_property =
			drm_property_create(dev, DRM_MODE_PROP_RANGE,
					    "color vibrance", 2);
		disp->color_vibrance_property->values[0] = 0;
		disp->color_vibrance_property->values[1] = 200; /* -100..+100 */
	}

	dev->mode_config.funcs = (void *)&nouveau_mode_config_funcs;
	dev->mode_config.fb_base = dev_priv->fb_phys;

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	if (dev_priv->card_type < NV_10) {
		dev->mode_config.max_width = 2048;
		dev->mode_config.max_height = 2048;
	} else
	if (dev_priv->card_type < NV_50) {
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	} else {
		dev->mode_config.max_width = 8192;
		dev->mode_config.max_height = 8192;
	}

#ifdef DRM_CAP_DUMB_PREFERRED_DEPTH
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;
#endif

	drm_kms_helper_poll_init(dev);
	drm_kms_helper_poll_disable(dev);

	ret = disp->create(dev);
	if (ret)
		return ret;

	if (dev->mode_config.num_crtc) {
		ret = drm_vblank_init(dev, 0);
		if (ret)
			return ret;
	}

	return ret;
}

void
nouveau_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_display_engine *disp = &dev_priv->engine.display;

	drm_vblank_cleanup(dev);

	disp->destroy(dev);

	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
}

