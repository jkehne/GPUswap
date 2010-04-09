/* BEGIN CSTYLED */

/*
 * Copyright (c) 2009, Intel Corporation.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "drmP.h"
#include <gfx_private.h>

/** @file drm_gem.c
 *
 * This file provides some of the base ioctls and library routines for
 * the graphics memory manager implemented by each device driver.
 *
 * Because various devices have different requirements in terms of
 * synchronization and migration strategies, implementing that is left up to
 * the driver, and all that the general API provides should be generic --
 * allocating objects, reading/writing data with the cpu, freeing objects.
 * Even there, platform-dependent optimizations for reading/writing data with
 * the CPU mean we'll likely hook those out to driver-specific calls.  However,
 * the DRI2 implementation wants to have at least allocate/mmap be generic.
 *
 * The goal was to have swap-backed object allocation managed through
 * struct file.  However, file descriptors as handles to a struct file have
 * two major failings:
 * - Process limits prevent more than 1024 or so being used at a time by
 *   default.
 * - Inability to allocate high fds will aggravate the X Server's select()
 *   handling, and likely that of many GL client applications as well.
 *
 * This led to a plan of using our own integer IDs (called handles, following
 * DRM terminology) to mimic fds, and implement the fd syscalls we need as
 * ioctls.  The objects themselves will still include the struct file so
 * that we can transition to fds if the required kernel infrastructure shows
 * up at a later date, and as our interface with shmfs for memory allocation.
 */

/*
 * We make up offsets for buffer objects so we can recognize them at
 * mmap time.
 */
#define DRM_FILE_PAGE_OFFSET_START ((0xFFFFFFFFUL >> PAGE_SHIFT) + 1)
#define DRM_FILE_PAGE_OFFSET_SIZE ((0xFFFFFFFFUL >> PAGE_SHIFT) * 16)

/**
 * Initialize the GEM device fields
 */

int
drm_gem_init(struct drm_device *dev)
{

	spin_lock_init(&dev->object_name_lock);
	idr_init(&dev->object_name_idr);
	atomic_set(&dev->object_count, 0);
	atomic_set(&dev->object_memory, 0);
	atomic_set(&dev->pin_count, 0);
	atomic_set(&dev->pin_memory, 0);
	atomic_set(&dev->gtt_count, 0);
	atomic_set(&dev->gtt_memory, 0);

	return 0;
}

void
drm_gem_destroy(struct drm_device *dev)
{
}

/*
 * Allocate a GEM object of the specified size with shmfs backing store
 */
struct drm_gem_object *
drm_gem_object_alloc(struct drm_device *dev, size_t size)
{
	static ddi_dma_attr_t dma_attr = {
		DMA_ATTR_V0,
		0U,				/* dma_attr_addr_lo */
		0xffffffffU,			/* dma_attr_addr_hi */
		0xffffffffU,			/* dma_attr_count_max */
		4096,				/* dma_attr_align */
		0x1fffU,			/* dma_attr_burstsizes */
		1,				/* dma_attr_minxfer */
		0xffffffffU,			/* dma_attr_maxxfer */
		0xffffffffU,			/* dma_attr_seg */
		1,				/* dma_attr_sgllen, variable */
		4,				/* dma_attr_granular */
		0				/* dma_attr_flags */
	};
	static ddi_device_acc_attr_t acc_attr = {
		DDI_DEVICE_ATTR_V0,
		DDI_NEVERSWAP_ACC,
		DDI_MERGING_OK_ACC
	};
	struct drm_gem_object *obj;
	ddi_dma_cookie_t cookie;
	uint_t cookie_cnt;
	drm_local_map_t *map;

	pgcnt_t real_pgcnt, pgcnt = btopr(size);
	uint32_t paddr, cookie_end;
	int i, n;

	obj = kmem_zalloc(sizeof (struct drm_gem_object), KM_NOSLEEP);
	if (obj == NULL)
		return (NULL);

	obj->dev = dev;
	obj->size = size;

	dma_attr.dma_attr_sgllen = (int)pgcnt;

	if (ddi_dma_alloc_handle(dev->devinfo, &dma_attr,
	    DDI_DMA_DONTWAIT, NULL, &obj->dma_hdl)) {
		DRM_ERROR("drm_gem_object_alloc: "
		    "ddi_dma_alloc_handle failed");
		goto err1;
	}
	if (ddi_dma_mem_alloc(obj->dma_hdl, ptob(pgcnt), &acc_attr,
	    IOMEM_DATA_UC_WR_COMBINE, DDI_DMA_DONTWAIT, NULL,
	    &obj->kaddr, &obj->real_size, &obj->acc_hdl)) {
		DRM_ERROR("drm_gem_object_alloc: "
		    "ddi_dma_mem_alloc failed");
		goto err2;
	}
	if (ddi_dma_addr_bind_handle(obj->dma_hdl, NULL,
	    obj->kaddr, obj->real_size, DDI_DMA_RDWR,
	    DDI_DMA_DONTWAIT, NULL, &cookie, &cookie_cnt)
	    != DDI_DMA_MAPPED) {
		DRM_ERROR("drm_gem_object_alloc: "
		    "ddi_dma_addr_bind_handle failed");
		goto err3;
	}

	real_pgcnt = btopr(obj->real_size);

	obj->pfnarray = kmem_zalloc(real_pgcnt * sizeof (pfn_t), KM_NOSLEEP);
	if (obj->pfnarray == NULL) {
		goto err4;
	}
	for (n = 0, i = 1; ; i++) {
		for (paddr = cookie.dmac_address,
		    cookie_end = cookie.dmac_address + cookie.dmac_size;
		    paddr < cookie_end;
		    paddr += PAGESIZE) {
			obj->pfnarray[n++] = btop(paddr);
			if (n >= real_pgcnt)
				goto addmap;
		}
		if (i >= cookie_cnt)
			break;
		ddi_dma_nextcookie(obj->dma_hdl, &cookie);
	}

addmap:
	map = drm_alloc(sizeof (struct drm_local_map), DRM_MEM_MAPS);
	if (map == NULL) {
		goto err5;
	}

	map->handle = obj;
	map->offset = (uintptr_t)map->handle;
	map->offset &= 0xffffffffUL;
	map->size = obj->real_size;
	map->type = _DRM_GEM;
	map->flags = _DRM_WRITE_COMBINING | _DRM_REMOVABLE;
	map->umem_cookie =
	    gfxp_umem_cookie_init(obj->kaddr, obj->real_size);
	if (map->umem_cookie == NULL) {
		goto err6;
	}

	obj->maplist.map = map;
	if (drm_map_handle(dev, &obj->maplist))
		goto err6;

	atomic_set(&obj->refcount, 1);
	atomic_set(&obj->handlecount, 1);
	if (dev->driver->gem_init_object != NULL &&
	    dev->driver->gem_init_object(obj) != 0) {
		goto err7;
	}
	atomic_inc(&dev->object_count);
	atomic_add(obj->size, &dev->object_memory);

	return (obj);

err7:
	gfxp_umem_cookie_destroy(map->umem_cookie);
err6:
	drm_free(map, sizeof (struct drm_local_map), DRM_MEM_MAPS);
err5:
	kmem_free(obj->pfnarray, real_pgcnt * sizeof (pfn_t));
err4:
	(void) ddi_dma_unbind_handle(obj->dma_hdl);
err3:
	ddi_dma_mem_free(&obj->acc_hdl);
err2:
	ddi_dma_free_handle(&obj->dma_hdl);
err1:
	kmem_free(obj, sizeof (struct drm_gem_object));

	return (NULL);
}

/**
 * Removes the mapping from handle to filp for this object.
 */
static int
drm_gem_handle_delete(struct drm_file *filp, u32 handle)
{
	struct drm_device *dev;
	struct drm_gem_object *obj;

	/* This is gross. The idr system doesn't let us try a delete and
	 * return an error code.  It just spews if you fail at deleting.
	 * So, we have to grab a lock around finding the object and then
	 * doing the delete on it and dropping the refcount, or the user
	 * could race us to double-decrement the refcount and cause a
	 * use-after-free later.  Given the frequency of our handle lookups,
	 * we may want to use ida for number allocation and a hash table
	 * for the pointers, anyway.
	 */
	spin_lock(&filp->table_lock);

	/* Check if we currently have a reference on the object */
	obj = idr_find(&filp->object_idr, handle);
	if (obj == NULL) {
		spin_unlock(&filp->table_lock);
		return -EINVAL;
	}
	dev = obj->dev;

	/* Release reference and decrement refcount. */
	idr_remove(&filp->object_idr, handle);
	spin_unlock(&filp->table_lock);

	mutex_lock(&dev->struct_mutex);
	drm_gem_object_handle_unreference(obj);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

/**
 * Create a handle for this object. This adds a handle reference
 * to the object, which includes a regular reference count. Callers
 * will likely want to dereference the object afterwards.
 */
int
drm_gem_handle_create(struct drm_file *file_priv,
		       struct drm_gem_object *obj,
		       u32 *handlep)
{
	int	ret;

	/*
	 * Get the user-visible handle using idr.
	 */
again:
	/* ensure there is space available to allocate a handle */
	if (idr_pre_get(&file_priv->object_idr, GFP_KERNEL) == 0)
		return -ENOMEM;

	/* do the allocation under our spinlock */
	spin_lock(&file_priv->table_lock);
	ret = idr_get_new_above(&file_priv->object_idr, obj, 1, (int *)handlep);
	spin_unlock(&file_priv->table_lock);
	if (ret == -EAGAIN)
		goto again;

	if (ret != 0)
		return ret;

	drm_gem_object_handle_reference(obj);
	return 0;
}

/** Returns a reference to the object named by the handle. */
struct drm_gem_object *
drm_gem_object_lookup(struct drm_device *dev, struct drm_file *filp,
		      u32 handle)
{
	struct drm_gem_object *obj;

	spin_lock(&filp->table_lock);

	/* Check if we currently have a reference on the object */
	obj = idr_find(&filp->object_idr, handle);
	if (obj == NULL) {
		spin_unlock(&filp->table_lock);
		return NULL;
	}

	drm_gem_object_reference(obj);

	spin_unlock(&filp->table_lock);

	return obj;
}

/**
 * Releases the handle to an mm object.
 */
int
drm_gem_close_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_gem_close *args = data;
	int ret;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	ret = drm_gem_handle_delete(file_priv, args->handle);

	return ret;
}

/**
 * Create a global name for an object, returning the name.
 *
 * Note that the name does not hold a reference; when the object
 * is freed, the name goes away.
 */
int
drm_gem_flink_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_gem_flink *args = data;
	struct drm_gem_object *obj;
	int ret;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return -EBADF;

again:
	if (idr_pre_get(&dev->object_name_idr, GFP_KERNEL) == 0) {
		ret = -ENOMEM;
		goto err;
	}

	spin_lock(&dev->object_name_lock);
	if (!obj->name) {
		ret = idr_get_new_above(&dev->object_name_idr, obj, 1,
					&obj->name);
		args->name = (uint64_t) obj->name;
		spin_unlock(&dev->object_name_lock);

		if (ret == -EAGAIN)
			goto again;

		if (ret != 0)
			goto err;

		/* Allocate a reference for the name table.  */
		drm_gem_object_reference(obj);
	} else {
		args->name = (uint64_t) obj->name;
		spin_unlock(&dev->object_name_lock);
		ret = 0;
	}

err:
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Open an object using the global name, returning a handle and the size.
 *
 * This handle (of course) holds a reference to the object, so the object
 * will not go away until the handle is deleted.
 */
int
drm_gem_open_ioctl(DRM_IOCTL_ARGS)
{
	struct drm_gem_open *args = data;
	struct drm_gem_object *obj;
	int ret;
	u32 handle;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	spin_lock(&dev->object_name_lock);
	obj = idr_find(&dev->object_name_idr, (int) args->name);
	if (obj)
		drm_gem_object_reference(obj);
	spin_unlock(&dev->object_name_lock);
	if (!obj)
		return -ENOENT;

	ret = drm_gem_handle_create(file_priv, obj, &handle);
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);
	if (ret)
		return ret;

	args->handle = handle;
	args->size = obj->size;

	return 0;
}

/**
 * Called at device open time, sets up the structure for handling refcounting
 * of mm objects.
 */
void
drm_gem_open(struct drm_device *dev, struct drm_file *file_private)
{
	idr_init(&file_private->object_idr);
	spin_lock_init(&file_private->table_lock);
}

/**
 * Called at device close to release the file's
 * handle references on objects.
 */
static int
drm_gem_object_release_handle(int id, void *ptr, void *data)
{
	struct drm_gem_object *obj = ptr;

	drm_gem_object_handle_unreference(obj);

	return 0;
}

/**
 * Called at close time when the filp is going away.
 *
 * Releases any remaining references on objects by this filp.
 */
void
drm_gem_release(struct drm_device *dev, struct drm_file *file_private)
{
	mutex_lock(&dev->struct_mutex);
	idr_for_each(&file_private->object_idr,
		     &drm_gem_object_release_handle, NULL);

	idr_destroy(&file_private->object_idr);
	mutex_unlock(&dev->struct_mutex);
}

/**
 * Called after the last reference to the object has been lost.
 *
 * Frees the object
 */
void
drm_gem_object_free(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_local_map *map = obj->maplist.map;

	if (dev->driver->gem_free_object != NULL)
		dev->driver->gem_free_object(obj);

	gfxp_umem_cookie_destroy(map->umem_cookie);
	drm_free(map, sizeof (struct drm_local_map), DRM_MEM_MAPS);

	kmem_free(obj->pfnarray, btopr(obj->real_size) * sizeof (pfn_t));

	(void) ddi_dma_unbind_handle(obj->dma_hdl);
	ddi_dma_mem_free(&obj->acc_hdl);
	ddi_dma_free_handle(&obj->dma_hdl);

	atomic_dec(&dev->object_count);
	atomic_sub(obj->size, &dev->object_memory);
	kmem_free(obj, sizeof (struct drm_gem_object));
}

/**
 * Called after the last handle to the object has been closed
 *
 * Removes any name for the object. Note that this must be
 * called before drm_gem_object_free or we'll be touching
 * freed memory
 */
void
drm_gem_object_handle_free(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;

	/* Remove any name for this object */
	spin_lock(&dev->object_name_lock);
	if (obj->name) {
		idr_remove(&dev->object_name_idr, obj->name);
		obj->name = 0;
		spin_unlock(&dev->object_name_lock);
		/*
		 * The object name held a reference to this object, drop
		 * that now.
		 */
		drm_gem_object_unreference(obj);
	} else
		spin_unlock(&dev->object_name_lock);

}
