

#define BLOCK_SIZE	PAGE_SIZE

#define NOUVEAU_PSCMM_DOMAIN_CPU       (1 << 0)
#define NOUVEAU_PSCMM_DOMAIN_VRAM      (1 << 1)
#define NOUVEAU_PSCMM_DOMAIN_GART      (1 << 2)

enum list_type {
	no_list,
	T1,
	T2,
	B1,
	B2,
	no_evicted,
};

struct drm_nouveau_private {

	/* ............. */

	struct pscmm_core *fb_block;

	struct list_head T1_list;
	struct list_head T2_list;
	struct list_head B1_list;
	struct list_head B2_list;

	uint32_t T1_num;
	uint32_t T2_num;
	uint32_t B1_num;
	uint32_t B2_num;

	uint32_t total_block_num;		//total block number
	uint32_t free_block_num;		//free block number
	uint32_t p = 0;					//

	struct list_head no_evicted_list;		//the bo which don't evicted
};

struct pscmm_core {

	// VRAM is split in block (could be page or bigger than page)

	// which channel each block belongs

	uint32_t *currentblockin;

	//
	struct drm_mm core_manager;	//VRAM phy mem manager
#if 0
	// Does the block pined?

	bool *block_pin;
	// block was allocated 1:allocated 0: no
	bool *block_used;
#endif


};

struct pscmm_channel {
	// this structure is private to each channel, it a simple bitmap used

	// to allocate GPU memory on behalf of the channel (one bit perblock)

	// allocation is only about finding enough 0 adjacent bit

	uint64_t bitmap_block;
};

struct pscmm_bo {

	u32 placements;
	struct pscmm_channel *channel;

	uint32_t tile_mode;
	uint32_t tile_flags;
	struct nouveau_tile_reg *tile;

	struct drm_gem_object *gem;

	uint32_t nblock;		//VRAM is split in block; the number of blocks
	uintptr_t firstblock;	//GPU vm address
	
	uintptr_t *block_array;	//the GPU physical address at which the bo is
	struct drm_mm_node *block_offset_node;	//same as block_array, used in drm_mm

	boot swap_out;		//the bo has been swap out

	struct list_head list;	//The object's place on the T1/T2/B1/B2 no_evict list
	enum list_type type;	//The object's in which listes
	enum list_type old_type;	//The object's in which listes before

	// bo reference bit
	bool bo_ref;

	
};

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

