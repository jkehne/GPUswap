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

/*
  * VRAM can be divide into two parts, non-page and page parts.
  * non-page includes scanout hardware-status page etc which will not be evicted.
  * page parts for user bos.
  * All the alloc, free, evicted, bind/unbind are for the page parts.
  */

/*
  * For bo alloc/free, I use page as unit and current drm_mm (buddy), still need to find a algorithm to find the free page more efficiently and less fragment. Still considering multi-processor, fast, high-usage.
  * For bo evict/replacement, I use bo as unit, modified CAR to do replacement.  Using bo is because it's simple and don't need to spend overhead to check each page. The disadvantage is if we need to swap, large bo is slower than part of bo.
  *
  * Rambling thought:
  * drm_mm bo alloc can't full use the VRAM, has some fragment. For using all, we need add a function to collect the fragment, when free_block_num is OK, but get_new_space failed. 
  * Does CAR good for replacement? This algorithm is for page, so for bo is it still good?
  * Features: I think most of the bos are used only once. swap cost to much.
  */


/*
  * need a algorithm to find the free page more efficiently and less fragment.
  */
uintptr_t *
get_new_space(struct pscmm_bo *nvbo, 
				struct drm_nouveau_private* dev_priv, uint32_t bnum)
{

/*
  * using drm_mm to do VRAM memory pages part alloc
  * need to modified to avoid the fragment
  */
	nvbo->block_offset_node = drm_mm_search_free(&dev_priv->fb_block->core_manager,
						    bnum, 0, 1);
	nvbo->block_offset_node = drm_mm_get_block(nvbo->block_offset_node,
						  bnum, 0);


	uintptr_t *block_array;
	int i;
	
	block_array = kzalloc(sizeof(*block_array) * bnum, GFP_KERNEL);
	for (i = 0; i < bnum; i++) {
		block_array[i] = nvbo->block_offset_node->start + sizeof(*block_array) * i;
	}
	return block_array;

}

uintptr_t *
evict_somthing(struct pscmm_bo *nvbo, 
				struct drm_nouveau_private* dev_priv, uint32_t bnum)	
{
	uintptr_t *block_array;
	struct pscmm_bo *nvbo_tmp;
	int i, j;
	int num = 0;
	int found;

	block_array = kzalloc(sizeof(*block_array) * bnum, GFP_KERNEL);

again:
	
	found = 0;
	do {
		if (dev_priv->T1_num >= max(1, p)) {
			nvbo_tmp = list_first_entry(dev_priv->T1_list, struct pscmm_bo, list);
			if (nvbo_tmp->bo_ref == 0) {
				found = 1;
				/* swap nvbo */

				list_move_tail(&nvbo_tmp->list, &dev_priv->B1_list);
				dev_priv->T1_num-=nvbo_tmp->nblock;
				dev_priv->B1_num+=nvbo_tmp->nblock;
				nvbo_tmp->type = B1;
				nvbo_tmp->placements;
			} else {
				list_move_tail(&nvbo_tmp->list, &dev_priv->T2_list);
				dev_priv->T1_num-=nvbo_tmp->nblock;
				dev_priv->T2_num+=nvbo_tmp->nblock;
				nvbo_tmp->type = T2;
			}
		} else {
			nvbo_tmp = list_first_entry(dev_priv->T2_list, struct pscmm_bo, list);
			if (nvbo_tmp->bo_ref == 0) {
				found = 1;
				/* swap nvbo */

				list_move_tail(&nvbo_tmp->list, &dev_priv->B2_list);
				dev_priv->T2_num-=nvbo_tmp->nblock;
				dev_priv->B2_num+=nvbo_tmp->nblock;
				nvbo_tmp->type = B2;
				nvbo_tmp->placements;
			} else {
				list_move_tail(&nvbo->list, &dev_priv->T2_list);
			}
		}
	} while(found);

	nvbo_tmp->swap_out = true;

	free_blocks(nvbo_tmp);

	dev_priv->free_block_num += nvbo_tmp->nblock;

	if (num < bnum)
		goto again;

	return get_new_space(nvbo, dev_priv, bnum); 
}


uintptr_t *
find_mem_space(struct drm_nouveau_private* dev_priv, struct pscmm_bo *nvbo,
					uint32_t bnum, bool no_evicted)
{



/*
  * Using modified CAR http://www.almaden.ibm.com/cs/people/dmodha/clockfast.pdf
  * Different: 
  * 1. c: total block number
  * 2. bo as schedule unit
  * 3. New list for non-evicted bo.  bo from non-evicted list will be added into tail of T1/T2 according to old_type and ref = 1
  * 4. CAR is used for replacement, need a good aglorithm to search free page in VRAM more efficiently, using drm_mm?
  * 5. if the bo was del, it will be removed directly from list.
  */
	struct pscmm_bo *temp;
	uintptr_t *block_array;
	if (bnum <= dev_priv->free_block_num) {
		
		block_array = get_new_space(nvbo, dev_priv, bnum); /* ret = -1 no mem*/

	} else {

		block_array = evict_somthing(nvbo, dev_priv, bnum);
		//update cache directory replacemet
		if ((nvbo->type != B1) && (nvbo->type != B2) &&
			((dev_priv->B1_num + dev_priv->T1_num) >= dev_priv->total_block_num)) {
			
			temp = list_first_entry(dev_priv->B1_list, struct pscmm_bo, list);
			list_del(&temp->list);
			dev_priv->B1_num -= temp->nblock;
		} else if ((nvbo->type != B1) && (nvbo->type != B2) &&
				((dev_priv->B1_num + dev_priv->T1_num + 
				dev_priv->B2_num + dev_priv->T2_num) >= dev_priv->total_block_num * 2)) {
			temp = list_first_entry(dev_priv->B2_list, struct pscmm_bo, list);
			list_del(&temp->list);
			dev_priv->B2_num -= temp->nblock;
		}
	}

	if ((nvbo->type != B1) && (nvbo->type != B2)) {
		list_add_tail(&nvbo->list, &dev_priv->T1_list);
		dev_priv->T1_num += bnum;
		nvbo->type = T1;
		nvbo->bo_ref = 0;
	} else if (nvbo->type == B1) {
		/* adapt */
		dev_priv->p = min{p+max{1, dev_priv->B2_num/dev_priv->B1_num}, dev_priv->total_block_num};

		list_move_tail(&nvbo->list, &dev_priv->T2_list);
		dev_priv->B1_num-=nvbo->nblock;
		dev_priv->T2_num+=nvbo->nblock;
		nvbo->type = T2;
		nvbo->bo_ref = 0;
		
	} else {
		/* adapt */
		dev_priv->p = max{p-max{1, dev_priv->B1_num/dev_priv->B2_num}, 0};

		list_move_tail(&nvbo->list, &dev_priv->T2_list);
		dev_priv->B2_num-=nvbo->nblock;
		dev_priv->T2_num+=nvbo->nblock;
		nvbo->type = T2;
		nvbo->bo_ref = 0;	
	}
	dev_priv->free_block_num -= bnum;

	if (no_evicted) {
		nvbo->old_type= nvbo->type;
		nvbo->type= no_evicted;

		list_move_tail(&nvbo->list, &dev_priv->no_evicted_list);
		if (nvbo->old_type == T1)
			dev_priv->T1_num-=nvbo->nblock;
		else
			dev_priv->T2_num-=nvbo->nblock;
	}
	
	return block_array;


}




/*
  * Update each block's status - pin/unpin
  */
void
free_blocks(pscmm_bo* nvbo)
{
	int i;
	int nblock;

	drm_mm_put_block(nvbo->block_offset_node);
}

/* Alloc mem from VRAM */
pscmm_bo *
pscmm_alloc(struct drm_nouveau_private* dev_priv, size_t bo_size, bool no_evicted)
{
	struct pscmm_bo *nvbo;
	uintptr_t *block_array;
	uint32_t bnum;
	int ret;

	bnum = bo_size / BLOCK_SIZE;
	nvbo = kzalloc(sizeof(struct pscmm_bo), GFP_KERNEL);

	/* find the blank VRAM and reserve */
	block_array = find_mem_space(dev_priv, nvbo, bnum, no_evicted);

	nvbo->channel = NULL;

	nvbo->nblock = bnum;		//VRAM is split in block 
	nvbo->block_array = block_array;			//the GPU physical address at which the bo is
	
	
	return nvbo;
}

void
pscmm_free(pscmm_bo* nvbo)
{

	/* remove from T1/T2/B1/B2 */
	free_blocks(nvbo);
	kfree(nvbo->block_array);
	kfree(nvbo);
}

int
pscmm_channel_map(struct pscmm_channel *chan, struct pscmm_bo *nvbo, 
			uint32_t low, uint32_t tail_flags, uintptr_t addr_ptr)
{
	int ret;

	/* Get free vm from per-channel page table using the function in drm_mm.c or bitmap_block*/

	/* bind the vm with the physical address in block_array */

	/* if use bitmap_block then update in pscmm_channel */
	
	return ret;
}

int
pscmm_channel_unmap(struct pscmm_channel *chan, struct pscmm_bo *nvbo)
{
	int ret;
	
	/* unbind */

	/* drm_mm_put_block or update the bitmap_block in pscmm_channel */
	
	return ret;
}

int
pscmm_move(struct drm_nouveau_private* dev_priv,  
				struct drm_gem_object* gem, struct pscmm_bo *nvbo, 
				uint32_t old_domain, uint32_t new_domain, bool no_evicted)
{
	int ret;

	/* no need to move */
	if (old_domain == new_domain)
		return ret;
	
	if (nvbo == NULL && new_domain == NOUVEAU_PSCMM_DOMAIN_VRAM) {
		/* alloc bo */

		nvbo = pscmm_alloc(dev_priv, gem->size, no_evicted)
		
 		/* alloc gem object */
		nvbo->gem = gem;

		nvbo->gem->driver_private = nvbo;

	}

	if (nvbo->placements == new_domain)
		return ret;

	/* check if the bo is non-evict */
	
	/* RAM -> VRAM */
	if (new_domain == NOUVEAU_PSCMM_DOMAIN_VRAM) {

	} else {
	/* VRAM -> RAM */

	}

	nvbo->placements = new_domain;
	
}

int
pscmm_set_normal(struct drm_nouveau_private *dev_priv,
					struct pscmm_bo *nvbo)
{
			nvbo->bo_ref = 1;
			if (nvbo->old_type == T1) {
				list_move_tail(&nvbo->list, &dev_priv->T1_list);
				dev_priv->T1_num+=nvbo->nblock;
			} else {
				list_move_tail(&nvbo->list, &dev_priv->T2_list);
				dev_priv->T2_num+=nvbo->nblock;
			}
			nvbo->type= nvbo->old_type;
			nvbo->old_type= no_evicted;
}

int
pscmm_prefault(struct drm_nouveau_private *dev_priv,
					struct pscmm_bo *nvbo)
{
//this is for object level prefault

		//nvbo swap out
		if (nvbo->swap_out) {
			kfree(nvbo->block_array);
			nvbo->block_array = find_mem_space(dev_priv, nvbo, nvbo->gem->size / BLOCK_SIZE, true);
			/* swap in ?*/
			copyback();
		} else {
			//hit
			nvbo->bo_ref = 1;
			nvbo->old_type= nvbo->type;
			nvbo->type= no_evicted;

			list_move_tail(&nvbo->list, &dev_priv->no_evicted_list);
			if (nvbo->old_type == T1)
				dev_priv->T1_num-=nvbo->nblock;
			else
				dev_priv->T2_num-=nvbo->nblock;
		}
		
}
int
pscmm_command_prefault(struct drm_nouveau_private *dev_priv, uint32_t obj_num,
					struct drm_nouveau_pscmm_exec_object * obj_list)
{
	struct drm_gem_object *gem;
	struct pscmm_bo *nvbo;
	int i, j;
	int ret;
	int nblock;
	

	
	for (i = 0; i < obj_num; i++) {

		gem = drm_gem_object_lookup(dev, file_priv, obj_list[i].handle);
	
		nvbo = gem ? gem->driver_private : NULL;

		/* check if nvbo is empty? */
		if (nvbo == NULL) {
			pscmm_move(dev_priv->fb_block, gem, nvbo, 0, NOUVEAU_PSCMM_DOMAIN_VRAM, true);

		}

		if (nvbo->type == no_evicted) {
			break;
		}
		
		pscmm_prefault(dev_priv, nvbo);
	}
	return ret;
}


int
nouveau_pscmm_ioctl_new(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_pscmm_new *arg = data;
	struct drm_gem_object *gem;
	int ret;

	/* page-align check */
	

 	/* alloc gem object */
	gem = drm_gem_object_alloc(dev, arg->size);

	ret = drm_gem_handle_create(file_priv, gem, &arg->handle);

	return ret;

}

int
nouveau_pscmm_ioctl_mmap(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pscmm_mmap *req = data;
	struct drm_gem_object *gem;
	struct pscmm_bo *nvbo;
	int ret;
	
	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	
	nvbo = gem ? gem->driver_private : NULL;

	if (nvbo != NULL) {
		
		if (nvbo->placements == NOUVEAU_PSCMM_DOMAIN_VRAM) {

			/* check if the bo is non-evict */
			if (nvbo->type != no_evicted) {
				/* prefault and mark the bo as non-evict*/
				pscmm_prefault(dev_priv, nvbo);
			} else {

				/* bo is accssed by GPU */
				return BUSY;
			}
				
			/* mmap the VRAM to user space 
			  * may or may not chanmap
			  * use the same FB BAR + tile_flags?
			  */

		}
				
		return ret;

	}

	
	/* mmap the GEM object to user space */
	
	return ret;
	
}


int
nouveau_pscmm_ioctl_range_flush(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pscmm_range_flush *arg = data;
	struct drm_gem_object *gem;
	struct pscmm_bo *nvbo;
	int ret;
	
	gem = drm_gem_object_lookup(dev, file_priv, arg->handle);
	
	nvbo = gem ? gem->driver_private : NULL;
	
	if (nvbo != NULL) {
		
		if (nvbo->placements == NOUVEAU_PSCMM_DOMAIN_VRAM) {
				
			/* flush to VRAM 
			  * may or may not chanmap
			  * use the same FB BAR + tile_flags?
			  */

			
			/* mark the block as normal*/
			pscmm_set_normal(dev_priv, nvbo);
        		return ret;
		}

	}
	
	/* Flush the GEM object */
	
	return ret;
}

int
nouveau_pscmm_chan_map_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_pscmm_chanmap *arg= data;
	struct drm_gem_object *gem;
	struct pscmm_bo *nvbo;
	struct pscmm_channel* chan;
	int need_sync = 0;
	int ret;

	gem = drm_gem_object_lookup(dev, file_priv, arg->handle);

	nvbo = gem ? gem->driver_private : NULL;

	/* get channel */
	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(arg->channel, file_priv, chan);
	
	if (nvbo = NULL) {

		pscmm_move(dev_priv, gem, nvbo, nvbo->placements, NOUVEAU_PSCMM_DOMAIN_VRAM, false);

	}

	/* bo can be shared between channels 
         * if bo has mapped to other chan, maybe do thing here
	  */


	/* chanmap, need low and tail_flags */
	ret = pscmm_channel_map(chan, nvbo, arg->low, arg->tail_flags, arg->addr_ptr);

	nvbo->channel = chan;
	nvbo->tile_flags = arg->tail_flags;
	nvbo->firstblock = arg->addr_ptr;

	return ret;
}

int
nouveau_pscmm_chan_unmap_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_pscmm_chanunmap *arg = data;
	struct drm_gem_object *gem;
	struct pscmm_bo *nvbo;
	int ret;
	
	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	
	nvbo = gem ? gem->driver_private : NULL;
	
	if (nvbo == NULL)
		return err;
	
	if (nvbo->channel != NULL) {
	
		/* unmap the channel */
		pscmm_channel_unmap(nvbo->channel, nvbo);
	}


}

int
nouveau_pscmm_ioctl_read(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pscmm_read *arg= data;
	struct drm_gem_object *gem;
	struct pscmm_bo *nvbo;
	int ret;
	
	gem = drm_gem_object_lookup(dev, file_priv, arg->handle);
	
	nvbo = gem ? gem->driver_private : NULL;

	if (nvbo != NULL) {
		
		if (nvbo->placements == NOUVEAU_PSCMM_DOMAIN_VRAM) {

			/* check if the bo is non-evict */
			if (nvbo->type == no_evicted) {
				/* wait rendering */

			}

			/* prefault and mark the bo as non-evict*/
			pscmm_prefault(dev_priv, nvbo);
				
			/* read the VRAM to user space address */

			/* mark the block as normal*/
			pscmm_set_normal(dev_priv, nvbo);
        		return ret;
		}

	}
	
	/* read the GEM object */
	
}


int
nouveau_pscmm_ioctl_write(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pscmm_write *req = data;
	struct drm_gem_object *gem;
	struct pscmm_bo *nvbo;
	int ret;
	
	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	
	nvbo = gem ? gem->driver_private : NULL;

	if (nvbo != NULL) {
		
		if (nvbo->placements == NOUVEAU_PSCMM_DOMAIN_VRAM) {
			/* check if the bo is non-evict */
			if (nvbo->type == no_evicted) {
				/* wait rendering */

			}

			/* prefault and mark the bo as non-evict*/
			pscmm_prefault(dev_priv, nvbo);
				
			/* write the VRAM to user space address */

			/* mark the block as normal*/
			pscmm_set_normal(dev_priv, nvbo);
				
        		return ret;
		}

	}
	
	/* Write the GEM object */
}


// The evict function will delete the bos.
// Normally the bo can be used only once, since the VRAM is limited and save the bo cost too much time in moving. 
// if the user want to save their bos, just move the bo to RAM.

int
nouveau_pscmm_ioctl_move(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_pscmm_move *arg = data;
	struct drm_gem_object *gem;
	struct pscmm_bo *nvbo;
	int ret;
	
	gem = drm_gem_object_lookup(dev, file_priv, arg->handle);
	
	nvbo = gem ? gem->driver_private : NULL;

	
	
	pscmm_move(dev_priv, gem, nvbo, arg->old_domain, arg->new_domain, false);

end:
		/* think about new is RAM. User space will ignore the firstblock if the bo is not in the VRAM*/
		arg->presumed_offset = nvbo->firstblock;		//bo gpu vm address
		arg->presumed_domain = nvbo->placements;	

}


int
nouveau_pscmm_ioctl_exec(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_pscmm_exec *args = data;
	struct drm_nouveau_pscmm_exec_command *command_list;
	struct drm_nouveau_pscmm_exec_object *obj_list;
	struct drm_gem_object *gem;
	struct pscmm_bo *nvbo;
	int i, j;
	int ret;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	
	nvbo = gem ? gem->driver_private : NULL;

	command_list = drm_malloc_ab(sizeof(*command_list), args->command_count);
	
	for (i = 0; i < req->command_count; i++) {
		ret = copy_from_user(command_list,
			     (struct drm_nouveau_pscmm_exec_command *)
			     (uintptr_t) args->command_ptr,
			     sizeof(*command_list) * args->command_count);

		obj_list = drm_malloc_ab(sizeof(*obj_list), command_list[i].buffer_count);

		for (j = 0; j < command_list[i].buffer_count; j++) {
			ret = copy_from_user(obj_list,
			     (struct drm_nouveau_pscmm_exec_object *)
			     (uintptr_t) command_list[i].buffers_ptr,
			     sizeof(*obj_list) * command_list[i].buffer_count);

			/* prefault and mark*/
			pscmm_command_prefault(dev_priv, command_list[i].buffer_count, obj_list)


			/* Copy the new  offsets back to the user's exec_object list. */
			ret = copy_to_user(obj_list,
			     (struct drm_nouveau_pscmm_exec_object *)
			     (uintptr_t) command_list[i].buffers_ptr,
			     sizeof(*obj_list) * command_list[i].buffer_count);
				
		}

		drm_free_large(obj_list);

		/* Add seqno into command buffer. */ 

		/* Emit the command buffer */

		/* Copy the seqno back to the user's exec_object list. */
		ret = copy_to_user(command_list,
			     (struct drm_nouveau_pscmm_exec_command *)
			     (uintptr_t) args->command_ptr,
			     sizeof(*command_list) * args->command_count);
	}

	drm_free_large(command_list);
}
