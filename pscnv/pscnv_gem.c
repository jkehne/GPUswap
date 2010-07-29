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
#include "pscnv_gem.h"
#include "pscnv_mem.h"
#include "pscnv_drm.h"

void pscnv_gem_free_object (struct drm_gem_object *obj) {
	struct pscnv_bo *vo = obj->driver_private;
	pscnv_mem_free(vo);
}

struct drm_gem_object *pscnv_gem_new(struct drm_device *dev, uint64_t size, uint32_t flags,
		uint32_t tile_flags, uint32_t cookie, uint32_t *user)
{
	int i;
	struct drm_gem_object *obj;
	struct pscnv_bo *vo;

	vo = pscnv_mem_alloc(dev, size, flags, tile_flags, cookie);
	if (!vo)
		return 0;

	obj = drm_gem_object_alloc(dev, vo->size);
	if (!obj) {
		pscnv_mem_free(vo);
		return 0;
	}
	obj->driver_private = vo;
	vo->gem = obj;

	if (user)
		for (i = 0; i < ARRAY_SIZE(vo->user); i++)
			vo->user[i] = user[i];
	else
		for (i = 0; i < ARRAY_SIZE(vo->user); i++)
			vo->user[i] = 0;

	return obj;
}

int pscnv_ioctl_gem_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_gem_info *info = data;
	struct drm_gem_object *obj;
	struct pscnv_bo *vo;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	obj = pscnv_gem_new(dev, info->size, info->flags, info->tile_flags, info->cookie, info->user);
	if (!obj) {
		return -ENOMEM;
	}
	vo = obj->driver_private;

	/* could change due to page size align */
	info->size = vo->size;

	ret = drm_gem_handle_create(file_priv, obj, &info->handle);

	if (pscnv_gem_debug >= 1)
		NV_INFO(dev, "GEM handle %x is VO %x/%d\n", info->handle, vo->cookie, vo->serial);

	info->map_handle = (uint64_t)info->handle << 32;
	drm_gem_object_handle_unreference_unlocked (obj);
	return ret;
}

int pscnv_ioctl_gem_info(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_pscnv_gem_info *info = data;
	struct drm_gem_object *obj;
	struct pscnv_bo *vo;
	int i;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	obj = drm_gem_object_lookup(dev, file_priv, info->handle);
	if (!obj)
		return -EBADF;

	vo = obj->driver_private;

	info->cookie = vo->cookie;
	info->flags = vo->flags;
	info->tile_flags = vo->tile_flags;
	info->size = obj->size;
	info->map_handle = (uint64_t)info->handle << 32;
	for (i = 0; i < ARRAY_SIZE(vo->user); i++)
		info->user[i] = vo->user[i];

	drm_gem_object_unreference_unlocked(obj);

	return 0;
}

