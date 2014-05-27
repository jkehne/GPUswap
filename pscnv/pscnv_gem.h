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

#ifndef __PSCNV_GEM_H__
#define __PSCNV_GEM_H__

struct pscnv_client;

void pscnv_gem_free_object (struct drm_gem_object *);
void pscnv_gem_close_object(struct drm_gem_object *obj, struct drm_file *file_priv);
struct drm_gem_object *pscnv_gem_new(struct drm_device *dev, uint64_t size,
		uint32_t flags,	uint32_t tile_flags, uint32_t cookie,
		uint32_t *user, struct pscnv_client *client);
struct drm_gem_object *pscnv_gem_wrap(struct drm_device *dev,
		struct pscnv_bo *vo);

#endif
