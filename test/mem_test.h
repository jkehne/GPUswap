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

#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include "drm.h"
#include <stdio.h>
#include "nouveau_drm.h"

#define NOUVEAU_PSCMM_DOMAIN_CPU       (1 << 0)
#define NOUVEAU_PSCMM_DOMAIN_VRAM      (1 << 1)
#define NOUVEAU_PSCMM_DOMAIN_GART      (1 << 2)

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
               
	uint32_t tile_flags;

	/**
	   * Length of data to map.
	   * The value will be page-aligned.
	   */

	uint64_t size;

	/** Offset in the object to map. */

	uint64_t offset;
				
	/** Returned pointer the data was mapped at */

	uint64_t addr_ptr;	/* void * */

};

struct nouveau_pscmm_range_flush {

	/** Handle for the object being mapped. */

	uint32_t handle;
				
	uint32_t pad;

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
               
	uint32_t tile_flags;

	/** Returned pointer the data was mapped at */

	uint64_t addr_ptr;	/* void * */

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
               
	uint32_t tile_mode;

	/** Length of data to read */

	uint64_t size;

	/** Offset into the object to read from */

	uint64_t offset;

	/**
	   * Pointer to write the data into.
	   */

	uint64_t data_ptr;

};


struct nouveau_pscmm_write {

	/** Handle for the object being written to. */

	uint32_t handle;

	/** Tiling mode */
               
	uint32_t tile_mode;

	/** Length of data to write */

	uint64_t size;

	/** Offset into the object to write to */

	uint64_t offset;

	/** Pointer to read the data from. */

	uint64_t data_ptr;	/* void * */

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

	uint64_t presumed_offset;

	/* Returned value of the updated domain of the object */

	uint32_t presumed_domain;

};


 struct drm_nouveau_pscmm_exec{

	uint32_t channel;

	/**
	   * This is a pointer to an array of drm_nouveau_pscmm_exec_object.
	   */

	uint32_t buffer_count;

	uint64_t buffers_ptr;

	/* Returned sequence number for sync*/

	uint32_t seqno;

};
		
		

struct drm_nouveau_pscmm_exec_object {

	uint32_t handle;

	uint32_t nr_dwords;

	/** Address of the object. */

	uint64_t add_ptr;

	/**
	   * Returned value of the updated address of the object
	   */

	uint64_t presumed_offset;

	/**
	   * Returned value of the updated domain of the object
	   */

	uint32_t presumed_domain;

};

struct nouveau_object {
        uint32_t handle;
        uint32_t tile_mode;
        uint32_t tile_flags;
        uint32_t placement;
        uint64_t size;
        uint64_t offset; //vram offset
        uint64_t chan_map;     //chan map offset
        uint32_t *gem_map;
        uint32_t channel;       //channel id
        uint32_t remaining;     //just for pushbuf

};

struct nouveau_grobj {
        struct nouveau_chan *channel;
        int grclass;
        uint32_t handle;

        enum {
                NOUVEAU_GROBJ_UNBOUND = 0,
                NOUVEAU_GROBJ_BOUND = 1,
                NOUVEAU_GROBJ_BOUND_EXPLICIT = 2
        } bound;
        int subc;
};

struct nouveau_subchannel {
        struct nouveau_grobj *gr;
        unsigned sequence;
};

struct nouveau_chan {
        int fd;
        int id;

        struct nouveau_object *pushbuf;
        uint32_t     pushbuf_domains;
        struct nouveau_grobj *nullobj;
        struct nouveau_grobj *vram;
        struct nouveau_grobj *gart;

        void *user_private;

        struct nouveau_subchannel subc[8];
        unsigned subc_sequence;
};

struct nouveau_notifier {
        struct nouveau_chan *channel;
        uint32_t handle;
        uint32_t size;
        uint32_t offset;
};

