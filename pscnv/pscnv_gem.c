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
#include "pscnv_gem.h"
#include "pscnv_mem.h"
#include "pscnv_drm.h"

void pscnv_gem_free_object (struct drm_gem_object *obj) {
	struct pscnv_bo *vo = obj->driver_private;
#ifndef PSCNV_KAPI_DRM_GEM_OBJECT_HANDLE_COUNT
	atomic_dec(&obj->handle_count);
#endif
	if (!vo->chan)
		pscnv_mem_free(vo);
	else
		vo->gem = 0;
#ifndef __linux__
	drm_gem_free_mmap_offset(obj);
#endif
	drm_gem_object_release(obj);
	kfree(obj);
}

struct drm_gem_object *pscnv_gem_wrap(struct drm_device *dev, struct pscnv_bo *vo)
{
	struct drm_gem_object *obj;

	obj = drm_gem_object_alloc(dev, vo->size);
	if (!obj)
		return 0;
#ifndef __linux__
	if (drm_gem_create_mmap_offset(obj) != 0) {
		drm_gem_object_handle_unreference_unlocked(obj);
		return 0;
	}
#endif

#ifndef PSCNV_KAPI_DRM_GEM_OBJECT_HANDLE_COUNT
	atomic_inc(&obj->handle_count);
#endif
	obj->driver_private = vo;
	vo->gem = obj;
	return obj;
}

struct drm_gem_object *pscnv_gem_new(struct drm_device *dev, uint64_t size, uint32_t flags,
		uint32_t tile_flags, uint32_t cookie, uint32_t *user)
{
	int i;
	struct pscnv_bo *vo = pscnv_mem_alloc(dev, size, flags, tile_flags, cookie);
	struct drm_gem_object *obj;
	if (!vo)
		return 0;

	if (!(obj = pscnv_gem_wrap(dev, vo)))
		pscnv_mem_free(vo);
	else if (user)
		for (i = 0; i < DRM_ARRAY_SIZE(vo->user); i++)
			vo->user[i] = user[i];
	else
		for (i = 0; i < DRM_ARRAY_SIZE(vo->user); i++)
			vo->user[i] = 0;

	return obj;
}
