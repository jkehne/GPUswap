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

struct drm_nouveau_pscmm_new {

	/**
	   * Requested size for the object.
	   */

	uint64_t size;

	/**
	   * Returned handle for the object.
	   *
	   * Object handles are nonzero.
	   */

	uint32_t handle;

};

struct nouveau_pscmm_mmap {

	/** Handle for the object being mapped. */

	uint32_t handle;
				
	/** Tiling flag */
               
	uint32_t tail_flags;

	/**
	   * Length of data to map.
	   * The value will be page-aligned.
	   */

	uint64_t size;

	/** Offset in the object to map. */

	uint64_t offset;
				
	/** Returned pointer the data was mapped at */

	uintptr_t addr_ptr;	/* void * */

};

struct nouveau_pscmm_range_flush {

	/** Handle for the object being mapped. */

	uint32_t handle;
				
	uint32_t pad

	/**
	   * Length of data to flush/dma.
	   */

	uint64_t size;

	/** Offset in the object to flush/dma. */

	uint64_t offset;

};

struct drm_nouveau_pscmm_chanmap {

	/** Handle for the object. */

	uint32_t handle;

	/** Handle for the channel. */

	uint32_t channel;

	/** mem needs to be in low-4GB range? */
				
	uint32_t low;

	/** Tiling flags */
               
	uint32_t tail_flags;

	/** Returned pointer the data was mapped at */

	uintptr_t addr_ptr;	/* void * */

};

struct drm_nouveau_pscmm_chanunmap {

	/** Handle for the object. */

	uint32_t handle;

	/** Handle for the channel. */

	uint32_t channel;

};


struct nouveau_pscmm_read {

	/** Handle for the object being read. */

	uint32_t handle;

	/** Tiling mode */
               
	uint32_t tail_mode;

	/** Length of data to read */

	uint64_t size;

	/** Offset into the object to read from */

	uint64_t offset;

	/**
	   * Pointer to write the data into.
	   */

	uintptr_t data_ptr;

};


struct nouveau_pscmm_write {

	/** Handle for the object being written to. */

	uint32_t handle;

	/** Tiling mode */
               
	uint32_t tail_mode;

	/** Length of data to write */

	uint64_t size;

	/** Offset into the object to write to */

	uint64_t offset;

	/** Pointer to read the data from. */

	uintptr_t data_ptr;	/* void * */

};

struct drm_nouveau_pscmm_move {

	/** Handle for the object */

	uint32_t handle;

	uint32_t pad;

	/** old place */

	uint32_t old_domain;		

	/** New place */

	uint32_t new_domain;

	/* * Returned value of the updated address of the object */

	uintptr_t presumed_offset;

	/* Returned value of the updated domain of the object */

	uint32_t presumed_domain;

};


 struct drm_nouveau_pscmm_exec {

	/**
	   * This is a pointer to an array of drm_nouveau_pscmm_exec_command.
	   */

	uint32_t command_count;
			
	uint32_t pad;

	uintptr_t command_ptr;

};

 struct drm_nouveau_pscmm_exec_command {

	uint32_t channel;

	/**
	   * This is a pointer to an array of drm_nouveau_pscmm_exec_object.
	   */

	uint32_t buffer_count;

	uintptr_t buffers_ptr;

	/* Returned sequence number for sync*/

	uint32_t seqno;

};
		
		

struct drm_nouveau_pscmm_exec_object {

	uint32_t handle;

	uint32_t pad;

	/** Address of the object. */

	uintptr_t add_ptr;

	/**
	   * Returned value of the updated address of the object
	   */

	uintptr_t presumed_offset;

	/**
	   * Returned value of the updated domain of the object
	   */

	uint32_t presumed_domain;

};


#define DRM_NOUVEAU_GETPARAM           0x00
#define DRM_NOUVEAU_SETPARAM           0x01
#define DRM_NOUVEAU_CHANNEL_ALLOC      0x02
#define DRM_NOUVEAU_CHANNEL_FREE       0x03
#define DRM_NOUVEAU_GROBJ_ALLOC        0x04
#define DRM_NOUVEAU_NOTIFIEROBJ_ALLOC  0x05
#define DRM_NOUVEAU_GPUOBJ_FREE        0x06
#define DRM_NOUVEAU_PSCMM_NEW            0x40
#define DRM_NOUVEAU_PSCMM_MMAP        0x41
#define DRM_NOUVEAU_PSCMM_RANGE_FLUSH       0x42
#define DRM_NOUVEAU_PSCMM_READ       0x43
#define DRM_NOUVEAU_PSCMM_WRITE           0x44
#define DRM_NOUVEAU_PSCMM_MOVE           0x45
#define DRM_NOUVEAU_PSCMM_EXEC           0x46
#define DRM_NOUVEAU_PSCMM_CHAN_MAP           0x47
#define DRM_NOUVEAU_PSCMM_CHAN_UNMAP           0x48
