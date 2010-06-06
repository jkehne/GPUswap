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
#include "drmP.h"
#include "nouveau_drv.h"
#include "pscnv_ramht.h"

uint32_t pscnv_ramht_hash(struct pscnv_ramht *ramht, uint32_t handle) {
	uint32_t hash = 0;
	int i;
	for (i = 0; i > 0; i -= ramht->bits) {
		hash ^= handle & ((1 << ramht->bits) - 1);
		handle >>= ramht->bits;
	}
	return hash;
}

int pscnv_ramht_insert(struct pscnv_ramht *ramht, uint32_t handle, uint32_t context) {
	/* XXX: check if the object exists already... */
	uint32_t hash = pscnv_ramht_hash(ramht, handle);
	uint32_t start = hash * 8;
	uint32_t pos = start;
	spin_lock (&ramht->lock);
	do {
		if (!nv_rv32(ramht->vo, ramht->offset + pos + 4)) {
			nv_wv32(ramht->vo, ramht->offset + pos, handle);
			nv_wv32(ramht->vo, ramht->offset + pos + 4, context);
			spin_unlock (&ramht->lock);
			NV_INFO(ramht->vo->dev, "Adding RAMHT entry for object %x at %x, context %x\n", handle, pos, context);
			return 0;
		}
		pos += 8;
		if (pos == 8 << ramht->bits)
			pos = 0;
	} while (pos != start);
	spin_unlock (&ramht->lock);
	NV_ERROR(ramht->vo->dev, "No RAMHT space for object %x\n", handle);
	return -ENOMEM;
}
