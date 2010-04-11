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

#define USE_REFCNT (dev_priv->card_type >= NV_10)
static timeout_id_t worktimer_id;

/*
  * need a algorithm to find the free page more efficiently and less fragment.
  */
uintptr_t *
get_new_space(struct nouveau_bo *nvbo, 
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
evict_somthing(struct nouveau_bo *nvbo, 
				struct drm_nouveau_private* dev_priv, uint32_t bnum)	
{
	uintptr_t *block_array;
	struct nouveau_bo *nvbo_tmp;
	int i, j;
	int num = 0;
	int found;

	block_array = kzalloc(sizeof(*block_array) * bnum, GFP_KERNEL);

again:
	
	found = 0;
	do {
		if (dev_priv->T1_num >= max(1, p)) {
			nvbo_tmp = list_first_entry(dev_priv->T1_list, struct nouveau_bo, list);
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
			nvbo_tmp = list_first_entry(dev_priv->T2_list, struct nouveau_bo, list);
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
find_mem_space(struct drm_nouveau_private* dev_priv, struct nouveau_bo *nvbo,
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
	struct nouveau_bo *temp;
	uintptr_t *block_array;
	if (bnum <= dev_priv->free_block_num) {
		
		block_array = get_new_space(nvbo, dev_priv, bnum); /* ret = -1 no mem*/

	} else {

		block_array = evict_somthing(nvbo, dev_priv, bnum);
		//update cache directory replacemet
		if ((nvbo->type != B1) && (nvbo->type != B2) &&
			((dev_priv->B1_num + dev_priv->T1_num) >= dev_priv->total_block_num)) {
			
			temp = list_first_entry(dev_priv->B1_list, struct nouveau_bo, list);
			list_del(&temp->list);
			dev_priv->B1_num -= temp->nblock;
		} else if ((nvbo->type != B1) && (nvbo->type != B2) &&
				((dev_priv->B1_num + dev_priv->T1_num + 
				dev_priv->B2_num + dev_priv->T2_num) >= dev_priv->total_block_num * 2)) {
			temp = list_first_entry(dev_priv->B2_list, struct nouveau_bo, list);
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
		pscmm_set_no_evicted(dev_priv, nvbo);
	}
	
	return block_array;


}




/*
  * Update each block's status - pin/unpin
  */
void
free_blocks(nouveau_bo* nvbo)
{
	int i;
	int nblock;

	drm_mm_put_block(nvbo->block_offset_node);
}

/* Alloc mem from VRAM */
nouveau_bo *
pscmm_alloc(struct drm_nouveau_private* dev_priv, size_t bo_size, bool no_evicted)
{
	struct nouveau_bo *nvbo;
	uintptr_t *block_array;
	uint32_t bnum;
	int ret;

	bnum = bo_size / BLOCK_SIZE;
	nvbo = kzalloc(sizeof(struct nouveau_bo), GFP_KERNEL);

	/* find the blank VRAM and reserve */
	block_array = find_mem_space(dev_priv, nvbo, bnum, no_evicted);

	nvbo->channel = NULL;

	nvbo->nblock = bnum;		//VRAM is split in block 
	nvbo->block_array = block_array;			//the GPU physical address at which the bo is


	return nvbo;
}

void
pscmm_free(nouveau_bo* nvbo)
{

	/* remove from T1/T2/B1/B2 */
	free_blocks(nvbo);
	kfree(nvbo->block_array);
	kfree(nvbo);
}

uintptr_t
nouveau_channel_map(struct drm_device *dev, struct nouveau_channel *chan, struct nouveau_bo *nvbo, 
			uint32_t low, uint32_t tail_flags)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uintptr_t addr_ptr;
	int ret;

	/* Get free vm from per-channel page table using the function in drm_mm.c or bitmap_block*/
	/* bind the vm with the physical address in block_array */
       /* Since the per-channel page table init at the channel_init and not changed then, */
	addr_ptr = nvbo->block_offset_node->start  + dev_priv->vm_vram_base;

	/* bind the vm */
	/* need? */
	ret = nv50_mem_vm_bind_linear(dev,
			nvbo->block_offset_node->start + dev_priv->vm_vram_base,
			nvbo->gem->size, nvbo->tile_flags,
			nvbo->block_offset_node->start);
	if (ret) {
		NV_ERROR(dev, "Failed to bind");
			return NULL;
	}
	/* if use bitmap_block then update in nouveau_channel, no need by now*/


	nvbo->channel = chan;
	nvbo->tile_flags = tail_flags;
	nvbo->firstblock = addr_ptr;
	nvbo->low = low;
		
	return addr_ptr;
}

int
nouveau_channel_unmap(struct drm_device *dev,
							struct nouveau_channel *chan, struct nouveau_bo *nvbo)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;
	
	/* unbind */
	/* need? */
	nv50_mem_vm_unbind(dev, nvbo->block_offset_node->start + dev_priv->vm_vram_base, nvbo->gem->size);
	/* drm_mm_put_block or update the bitmap_block in nouveau_channel */
	nvbo->channel = NULL;
	
	return ret;
}

static int
pscmm_move_memcpy(struct drm_device *dev,  
				struct drm_gem_object* gem, struct nouveau_bo *nvbo, 
				uint32_t old_domain, uint32_t new_domain, bool no_evicted)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	/* only support RAM->VRAM & VRAM->RAM */
	if (nvbo->virtual == NULL)
		nvbo->virtual = ioremap(dev_priv->fb_block->io_offset + nvbo->block_offset_node->start,  gem->size);
	
	if (old_domain == NOUVEAU_PSCMM_DOMAIN_CPU && 
		new_domain == NOUVEAU_PSCMM_DOMAIN_VRAM) {
		memcpy(gem->kaddr, nvbo->virtual, gem->size);
	} else if (old_domain == NOUVEAU_PSCMM_DOMAIN_VRAM && 
		new_domain == NOUVEAU_PSCMM_DOMAIN_CPU) {
		memcpy(nvbo->virtual, gem->kaddr, gem->size);
	} else 
		NV_ERROR(dev, "Not support %d -> %d copy", old_domain, new_domain);
}

static int
pscmm_move_m2mf(struct drm_nouveau_private* dev_priv,  
				struct drm_gem_object* gem, struct nouveau_bo *nvbo, 
				uint32_t old_domain, uint32_t new_domain, bool no_evicted)
{
#if 0
	struct nouveau_channel *chan;
	uint64_t src_offset, dst_offset;
	uint32_t page_count;
	int ret;

	chan = nvbo->channel;
	if (!chan || nvbo->tile_flags || nvbo->no_vm)
		chan = dev_priv->channel;

	src_offset = old_mem->mm_node->start << PAGE_SHIFT;
	dst_offset = new_mem->mm_node->start << PAGE_SHIFT;
	if (chan != dev_priv->channel) {
		if (old_mem->mem_type == TTM_PL_TT)
			src_offset += dev_priv->vm_gart_base;
		else
			src_offset += dev_priv->vm_vram_base;

		if (new_mem->mem_type == TTM_PL_TT)
			dst_offset += dev_priv->vm_gart_base;
		else
			dst_offset += dev_priv->vm_vram_base;
	}

	ret = RING_SPACE(chan, 3);
	if (ret)
		return ret;
	BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_DMA_SOURCE, 2);
	OUT_RING(chan, nouveau_bo_mem_ctxdma(nvbo, chan, old_mem));
	OUT_RING(chan, nouveau_bo_mem_ctxdma(nvbo, chan, new_mem));

	if (dev_priv->card_type >= NV_50) {
		ret = RING_SPACE(chan, 4);
		if (ret)
			return ret;
		BEGIN_RING(chan, NvSubM2MF, 0x0200, 1);
		OUT_RING(chan, 1);
		BEGIN_RING(chan, NvSubM2MF, 0x021c, 1);
		OUT_RING(chan, 1);
	}

	page_count = new_mem->num_pages;
	while (page_count) {
		int line_count = (page_count > 2047) ? 2047 : page_count;

		if (dev_priv->card_type >= NV_50) {
			ret = RING_SPACE(chan, 3);
			if (ret)
				return ret;
			BEGIN_RING(chan, NvSubM2MF, 0x0238, 2);
			OUT_RING(chan, upper_32_bits(src_offset));
			OUT_RING(chan, upper_32_bits(dst_offset));
		}
		ret = RING_SPACE(chan, 11);
		if (ret)
			return ret;
		BEGIN_RING(chan, NvSubM2MF,
				 NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN, 8);
		OUT_RING(chan, lower_32_bits(src_offset));
		OUT_RING(chan, lower_32_bits(dst_offset));
		OUT_RING(chan, PAGE_SIZE); /* src_pitch */
		OUT_RING(chan, PAGE_SIZE); /* dst_pitch */
		OUT_RING(chan, PAGE_SIZE); /* line_length */
		OUT_RING(chan, line_count);
		OUT_RING(chan, (1<<8)|(1<<0));
		OUT_RING(chan, 0);
		BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_NOP, 1);
		OUT_RING(chan, 0);

		page_count -= line_count;
		src_offset += (PAGE_SIZE * line_count);
		dst_offset += (PAGE_SIZE * line_count);
	}

	/* Add seqno into command buffer. */ 
	/* we can check the seqno onec it's done. */
	/* can we use notifier obj here? */
	return ret;
#endif
	
}

int
pscmm_move(struct drm_device *dev,  
				struct drm_gem_object* gem, struct nouveau_bo *nvbo, 
				uint32_t old_domain, uint32_t new_domain, bool no_evicted)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
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
	if (nvbo->type == no_evicted) {
		/* wait rendering or return? */
		NV_ERROR(dev, "nvbo is busy");
		return -1;
	}


	/* we should use Hardware assisted copy here*/
	/* need to fix */
	pscmm_move_memcpy(dev, gem, nvbo, old_domain, new_domain);
	//pscmm_move_m2mf(dev_priv, gem, nvbo, old_domain, new_domain);
	
	nvbo->placements = new_domain;

	if (no_evicted) {
		pscmm_set_no_evicted(dev_priv, nvbo);
	}

	return ret;
}

void
pscmm_move_active_list(struct drm_nouveau_private *dev_priv,
							struct drm_gem_object *gem, struct nouveau_bo *nvbo)
{
	if (!nvbo->active) {
		drm_gem_object_reference(gem);
		nvbo->active = 1;
	}
	list_move_tail(&nvbo->active_list, &dev_priv->active_list);
}

void
pscmm_remove_active_list(struct drm_nouveau_private *dev_priv,
							struct drm_gem_object *gem, struct nouveau_bo *nvbo)
{

	list_del_init(&nvbo->active_list);

	nvbo->last_rendering_seqno = 0;	
	if (nvbo->active) {
		nvbo->active = 0;
		drm_gem_object_unreference(gem);
	}
}

int
pscmm_set_no_evicted(struct drm_nouveau_private *dev_priv,
					struct nouveau_bo *nvbo)
{

		nvbo->last_rendering_seqno = 0xFFFFFFFF;
		
		nvbo->old_type= nvbo->type;
		nvbo->type= no_evicted;

		list_move_tail(&nvbo->list, &dev_priv->no_evicted_list);
		if (nvbo->old_type == T1)
			dev_priv->T1_num-=nvbo->nblock;
		else
			dev_priv->T2_num-=nvbo->nblock;
}
int
pscmm_set_normal(struct drm_nouveau_private *dev_priv,
					struct nouveau_bo *nvbo)
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
					struct nouveau_bo *nvbo)
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
			pscmm_set_no_evicted(dev_priv, nvbo);
		}
}
int
pscmm_command_prefault(struct drm_device *dev, struct drm_file *file_priv, uint32_t handle)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	int i, j;
	int ret;
	int nblock;

	gem = drm_gem_object_lookup(dev, file_priv, handle);
	
	nvbo = gem ? gem->driver_private : NULL;

	/* check if nvbo is empty? */
	if (nvbo == NULL) {
		pscmm_move(dev, gem, nvbo, 0, NOUVEAU_PSCMM_DOMAIN_VRAM, true);
	}

	if (nvbo->type == no_evicted) {
		break;
	}
		
	pscmm_prefault(dev_priv, nvbo);

	return ret;
}

/**
 * Returns true if seq1 is later than seq2.
 */
static int
nouveau_seqno_passed(uint32_t seq1, uint32_t seq2)
{
	return (int32_t)(seq1 - seq2) >= 0;
}

/**
  * Get seq from globle status page
  */
uint32_t
nouveau_get_pscmm_seqno(struct nouveau_channel* chan)
{

	return nvchan_rd32(chan, 0x48);
}

static void
pscmm_retire_request(struct drm_device *dev,
			struct drm_nouveau_pscmm_request *request)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	/* move bo out of no_evicted list */
	while (!list_empty(&dev_priv->active_list)) {
		struct nouveau_bo *nvbo;

		nvbo = list_entry(dev_priv->active_list,
					    struct nouveau_bo,
					    active_list);

		/* If the seqno being retired doesn't match the oldest in the
		 * list, then the oldest in the list must still be newer than
		 * this seqno.
		 */
		if (nvbo->last_rendering_seqno != request->seqno)
			break;

		pscmm_remove_active_list(dev_priv, nvbo->gem, nvbo);
	}

}

/**
 * This function clears the request list as sequence numbers are passed.
 */
void
nouveau_pscmm_retire_requests(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel* chan = dev_priv->channel;
	uint32_t seqno;

	seqno = nouveau_get_pscmm_seqno(chan);

	while (!list_empty(&chan->request_list)) {
		struct drm_nouveau_pscmm_request *request;
		uint32_t retiring_seqno;
		request = (struct drm_nouveau_pscmm_request *)(uintptr_t)(dev_priv->request_list.next->contain_ptr);
		retiring_seqno = request->seqno;

		if (nouveau_seqno_passed(seqno, retiring_seqno) ) {
			pscmm_retire_request(dev, request);

			list_del(&request->list);
			drm_free(request, sizeof(*request), DRM_MEM_DRIVER);
		} else
			break;
	}
}

void
nouveau_pscmm_retire_work_handler(void *device)
{
	struct drm_device *dev = (struct drm_device *)device;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel* chan = dev_priv->channel;

	/* Return if gem idle */
	if (worktimer_id == NULL) {
		return;
	}

	nouveau_pscmm_retire_requests(dev);
	if (!list_empty(&chan->request_list))
	{	
		NV_DEBUG("schedule_delayed_work");
		worktimer_id = timeout(nouveau_pscmm_retire_work_handler, (void *) dev, DRM_HZ);
	}

}

static uint32_t
pscmm_add_request(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_pscmm_request *request;
	uint32_t seqno;
	int ret;
	int was_empty;
	struct nouveau_channel* chan = dev_priv->channel;

	request = drm_calloc(1, sizeof(*request), DRM_MEM_DRIVER);
	if (request == NULL) {
		DRM_ERROR("Failed to alloc request");
		return 0;
	}
	
	ret = RING_SPACE(chan, 2);
	seqno = chan->next_seqno;		/* globle seqno */
	chan->next_seqno++;
	if (chan->next_seqno == 0)
		chan->next_seqno++;


	BEGIN_RING(chan, NvSubSw, USE_REFCNT ? 0x0050 : 0x0150, 1);
	OUT_RING(chan, seqno);
	FIRE_RING(chan);

	request->seqno = seqno;
	request->emitted_jiffies = jiffies;
	request->chan = chan;

	was_empty = list_empty(&chan->request_list);
	list_add_tail(&request->list, &chan->request_list, (caddr_t)request);

	if (was_empty)
	{
		/* change to delay HZ and then run work (not insert to workqueue of Linux) */ 
		worktimer_id = timeout(nouveau_pscmm_retire_work_handler, (void *) chan, DRM_HZ);
		DRM_DEBUG("i915_gem: schedule_delayed_work");
	}
	return seqno;
}


void
nouveau_pscmm_remove(struct drm_device *dev,  struct nouveau_bo *nvbo)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	
	if (nvbo->virtual) {
		iounmap(nvbo->virtual);
	}

	pscmm_set_normal(dev_priv, nvbo);

}

int
nouveau_pscmm_new(struct drm_device *dev,  struct drm_file *file_priv,
		int size, int align, uint32_t flags,
		bool no_evicted, bool mappable,
		struct nouveau_bo **pnvbo)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_bo *nvbo;
	u32 handle;
	struct drm_gem_object *gem;
	int ret;
	
 	/* alloc gem object */
	gem = drm_gem_object_alloc(dev, size);

	if (file_priv)
		ret = drm_gem_handle_create(file_priv, gem, &handle);

	/* if vram, need to move */
	if (flags == MEM_PL_FLAG_VRAM) {
		pscmm_move(dev, gem, nvbo, NOUVEAU_PSCMM_DOMAIN_CPU, NOUVEAU_PSCMM_DOMAIN_VRAM, no_evicted);
	}

	if (mappable) {
		nvbo->virtual = ioremap(dev_priv->fb_block->io_offset + nvbo->block_offset_node->start,  gem->size);
	}

	*pnvbo = nvbo;
	return 0;
}

int
nouveau_pscmm_ioctl_new(DRM_IOCTL_ARGS)
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
nouveau_pscmm_ioctl_mmap(DRM_IOCTL_ARGS)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pscmm_mmap *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	int ret;


/* need fix */
NV_ERROR(dev, "Not support by now");
return -1;


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
nouveau_pscmm_ioctl_range_flush(DRM_IOCTL_ARGS)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pscmm_range_flush *arg = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	int ret;


/* need fix */
NV_ERROR(dev, "Not support by now");
return -1;

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
nouveau_pscmm_ioctl_chan_map(DRM_IOCTL_ARGS)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_pscmm_chanmap *arg= data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	struct nouveau_channel* chan;
	int need_sync = 0;
	int ret;

	gem = drm_gem_object_lookup(dev, file_priv, arg->handle);

	nvbo = gem ? gem->driver_private : NULL;

	/* get channel */
	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(arg->channel, file_priv, chan);
	
	if (nvbo == NULL) {

		pscmm_move(dev, gem, nvbo, NOUVEAU_PSCMM_DOMAIN_CPU, NOUVEAU_PSCMM_DOMAIN_VRAM, false);

	}


	if (!nvbo->channel) {
		/* chanmap, need low and tail_flags */
		arg->addr_ptr = nouveau_channel_map(dev, chan, nvbo, arg->low, arg->tail_flags);

	} else {
		/* bo can be shared between channels 
         	  * if bo has mapped to other chan, maybe do something here
	 	  */
	 	NV_DEBUG(dev, "bo shared between channels are not supported by now");
	}

	return ret;
}

int
nouveau_pscmm_ioctl_chan_unmap(DRM_IOCTL_ARGS)
{
	struct drm_nouveau_pscmm_chanunmap *arg = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	int ret;
	
	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	
	nvbo = gem ? gem->driver_private : NULL;
	
	if (nvbo == NULL)
		return err;
	
	if (nvbo->channel != NULL) {
	
		/* unmap the channel */
		nouveau_channel_unmap(dev, nvbo->channel, nvbo);
	}


}

int
nouveau_pscmm_ioctl_read(DRM_IOCTL_ARGS)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pscmm_read *arg= data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	void *addr;
	unsigned long unwritten = 0;
	uint32_t *user_data;
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
			addr = ioremap(dev_priv->fb_block->io_offset + nvbo->block_offset_node->start,  gem->size);
			if (!addr) {
				NV_ERROR(dev, "bo shared between channels are not supported by now");
				return -ENOMEM;
			}

			user_data = (uint32_t *) (uintptr_t) arg->data_ptr;
			unwritten = DRM_COPY_TO_USER(user_data, addr + arg->offset, arg->size);
       		if (unwritten) {
                		ret = EFAULT;
                		NV_ERROR(dev, "failed to read, unwritten %d", unwritten);
        		}
			
			
			/* mark the block as normal*/
			pscmm_set_normal(dev_priv, nvbo);
        		return ret;
		}

	}
	
	/* read the GEM object */
	user_data = (uint32_t *) (uintptr_t) arg->data_ptr;
	unwritten = DRM_COPY_TO_USER(user_data, gem->kaddr + arg->offset, arg->size);
        if (unwritten) {
                ret = EFAULT;
                DRM_ERROR("i915_gem_pread error!!! unwritten %d", unwritten);
        }
	
}


int
nouveau_pscmm_ioctl_write(DRM_IOCTL_ARGS)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pscmm_write *arg = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	void *addr;
	unsigned long unwritten = 0;
	uint32_t *user_data;
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
				
			/* write the VRAM to user space address */
			addr = ioremap(dev_priv->fb_block->io_offset + nvbo->block_offset_node->start,  gem->size);
			if (!addr) {
				NV_ERROR(dev, "bo shared between channels are not supported by now");
				return -ENOMEM;
			}

			user_data = (uint32_t *) (uintptr_t) arg->data_ptr;
			unwritten = DRM_COPY_FROM_USER( addr + arg->offset, user_data, arg->size);
       		if (unwritten) {
                		ret = EFAULT;
                		NV_ERROR(dev, "failed to read, unwritten %d", unwritten);
        		}

			
			/* mark the block as normal*/
			pscmm_set_normal(dev_priv, nvbo);
				
        		return ret;
		}

	}
	
	/* Write the GEM object */
	user_data = (uint32_t *) (uintptr_t) arg->data_ptr;
	unwritten = DRM_COPY_FROM_USER(gem->kaddr + arg->offset, user_data, arg->size);
        if (unwritten) {
                ret = EFAULT;
                NV_ERROR("i915_gem_gtt_pwrite error!!! unwritten %d", unwritten);
                return ret;
        }
	
}


// The evict function will delete the bos.
// Normally the bo can be used only once, since the VRAM is limited and save the bo cost too much time in moving. 
// if the user want to save their bos, just move the bo to RAM.

int
nouveau_pscmm_ioctl_move(DRM_IOCTL_ARGS)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_pscmm_move *arg = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	int ret;
	
	gem = drm_gem_object_lookup(dev, file_priv, arg->handle);
	
	nvbo = gem ? gem->driver_private : NULL;
	
	pscmm_move(dev, gem, nvbo, arg->old_domain, arg->new_domain, false);

end:
		/* think about new is RAM. User space will ignore the firstblock if the bo is not in the VRAM*/
		arg->presumed_offset = nvbo->firstblock;		//bo gpu vm address
		arg->presumed_domain = nvbo->placements;	

}


int
nouveau_pscmm_ioctl_exec(DRM_IOCTL_ARGS)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_pscmm_exec *args = data;
	struct drm_nouveau_pscmm_exec_command *command_list;
	struct drm_nouveau_pscmm_exec_object *obj_list;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	struct nouveau_channel* chan;
	int i, j;
	int ret;

	command_list = drm_malloc_ab(sizeof(*command_list), args->command_count);

	ret = copy_from_user(command_list,
			     (struct drm_nouveau_pscmm_exec_command *)
			     (uintptr_t) args->command_ptr,
			     sizeof(*command_list) * args->command_count);
	
	for (i = 0; i < args->command_count; i++) {
		chan = command_list[i].channel;
		obj_list = drm_malloc_ab(sizeof(*obj_list), command_list[i].buffer_count);

		ret = copy_from_user(obj_list,
			     (struct drm_nouveau_pscmm_exec_object *)
			     (uintptr_t) command_list[i].buffers_ptr,
			     sizeof(*obj_list) * command_list[i].buffer_count);
		for (j = 0; j < command_list[i].buffer_count; j++) {

			/* prefault and mark*/
			pscmm_command_prefault(dev, file_priv, obj_list[j].handle);
				
		}

		/* Copy the new  offsets back to the user's exec_object list. */
		ret = copy_to_user(obj_list,
			     (struct drm_nouveau_pscmm_exec_object *)
			     (uintptr_t) command_list[i].buffers_ptr,
			     sizeof(*obj_list) * command_list[i].buffer_count);
			
		gem = drm_gem_object_lookup(dev, file_priv, obj_list[j-1].handle);
	
		nvbo = gem ? gem->driver_private : NULL;

		/* Emit the command buffer */
		nv50_dma_push(chan, nvbo, 0, gem->size);

		/* Add seqno into command buffer. */ 
		command_list[i].seqno = pscmm_add_request(dev);
		for (j = 0; j < command_list[i].buffer_count; j++) {

			gem = drm_gem_object_lookup(dev, file_priv, obj_list[j].handle);
	
			nvbo = gem ? gem->driver_private : NULL;
			nvbo->last_rendering_seqno = command_list[i].seqno;
			pscmm_move_active_list(dev_priv, gem, nvbo);
		}

		drm_free_large(obj_list);

	}
		/* Copy the seqno back to the user's exec_object list. */
	ret = copy_to_user(command_list,
			     (struct drm_nouveau_pscmm_exec_command *)
			     (uintptr_t) args->command_ptr,
			     sizeof(*command_list) * args->command_count);

	drm_free_large(command_list);
}


/***********************************
 * finally, the ioctl table
 ***********************************/

struct drm_ioctl_desc nouveau_ioctls[] = {
	DRM_IOCTL_DEF(DRM_NOUVEAU_GETPARAM, nouveau_ioctl_getparam, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_SETPARAM, nouveau_ioctl_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_NOUVEAU_CHANNEL_ALLOC, nouveau_ioctl_fifo_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_CHANNEL_FREE, nouveau_ioctl_fifo_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GROBJ_ALLOC, nouveau_ioctl_grobj_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_NOTIFIEROBJ_ALLOC, nouveau_ioctl_notifier_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GPUOBJ_FREE, nouveau_ioctl_gpuobj_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_PSCMM_NEW, nouveau_pscmm_ioctl_new, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_PSCMM_MMAP, nouveau_pscmm_ioctl_mmap, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_PSCMM_RANGE_FLUSH, nouveau_pscmm_ioctl_range_flush, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_PSCMM_READ, nouveau_pscmm_ioctl_read, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_PSCMM_WRITE, nouveau_pscmm_ioctl_write, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_PSCMM_MOVE, nouveau_pscmm_ioctl_move, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_PSCMM_EXEC, nouveau_pscmm_ioctl_exec, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_PSCMM_CHAN_MAP, nouveau_pscmm_ioctl_chan_map, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_PSCMM_CHAN_UNMAP, nouveau_pscmm_ioctl_chan_unmap, DRM_AUTH),
};

int nouveau_max_ioctl = DRM_ARRAY_SIZE(nouveau_ioctls);

