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
	struct drm_device *dev = obj->dev;
	struct pscnv_bo *vo = obj->driver_private;
	
	WARN_ON(vo->gem != obj);
	
#ifndef PSCNV_KAPI_DRM_GEM_OBJECT_HANDLE_COUNT
	atomic_dec(&obj->handle_count);
#endif

	if (pscnv_mem_debug >= 1) {
		NV_INFO(dev, "pscnv_gem_free_object: cookie=%08x/%d "
			"(bo.refcnt=%d, drm.refcnt=%d, drm.handlecnt=%d)\n",
			vo->cookie, vo->serial,
			atomic_read(&vo->ref.refcount),
			atomic_read(&obj->refcount.refcount),
			atomic_read(&obj->handle_count));
	}

#ifndef __linux__
	drm_gem_free_mmap_offset(obj);
#endif
	/* these should not be called! drm_gem_object_free_common() will do
	   this.
	   drm_gem_object_free_common() is called by drm_object_free() after
	   this function.
	
	drm_gem_object_release(obj);
	kfree(obj); */
	
	vo->gem = NULL;
	if (pscnv_mem_debug >= 2) {
		NV_INFO(dev, "gem_free_object: unref BO%08x/%d\n", vo->cookie, vo->serial);
	}
	pscnv_bo_unref(vo);
}

void pscnv_gem_close_object(struct drm_gem_object *obj, struct drm_file *file_priv)
{
	struct drm_device *dev = obj->dev;
	struct pscnv_bo *vo = obj->driver_private;
	
	if (pscnv_mem_debug >= 1) {
		NV_INFO(dev, "pscnv_gem_close_object: cookie=%08x/%d "
			"(bo.refcnt=%d, drm.refcnt=%d, drm.handlecnt=%d)\n",
			vo->cookie, vo->serial,
			atomic_read(&vo->ref.refcount),
			atomic_read(&obj->refcount.refcount),
			atomic_read(&obj->handle_count));
	}
	
	/* nothing to do here, see drm_gem_handle_delete, which calls this function */
}

struct drm_gem_object *pscnv_gem_wrap(struct drm_device *dev, struct pscnv_bo *vo)
{
	struct drm_gem_object *obj;
	
	if (pscnv_mem_debug >= 1) {
		NV_INFO(dev, "pscnv_gem_wrap: cookie=%08x/%d (bo.refcnt=%d)\n", vo->cookie, vo->serial, atomic_read(&vo->ref.refcount));
	}

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
	if (pscnv_mem_debug >= 2) {
		NV_INFO(dev, "pscnv_gem_wrap: ref BO%08x/%d\n", vo->cookie, vo->serial);
	}
	pscnv_bo_ref(vo);
	
	obj->driver_private = vo;
	vo->gem = obj;
	return obj;
}

struct drm_gem_object *pscnv_gem_new(struct drm_device *dev, uint64_t size, uint32_t flags,
		uint32_t tile_flags, uint32_t cookie, uint32_t *user, struct pscnv_client *client)
{
	int i;
	struct pscnv_bo *vo = pscnv_mem_alloc(dev, size, flags, tile_flags, cookie, client);
	struct drm_gem_object *obj;
	if (!vo)
		return 0;
	
	if (!(obj = pscnv_gem_wrap(dev, vo))) {
		pscnv_mem_free(vo);
		return NULL;
	}
	
	for (i = 0; i < DRM_ARRAY_SIZE(vo->user); i++) {
			vo->user[i] = (user) ? user[i] : 0;
	}
	
	/* gem_wrap increases ref-count. We don't need the buffer object, so
	   the user may free it (by closing the gem) anytime */
	if (pscnv_mem_debug >= 2) {
		NV_INFO(dev, "pscnv_gem_new: unref BO%08x/%d", vo->cookie, vo->serial);
	}
	pscnv_bo_unref(vo);

	return obj;
}
