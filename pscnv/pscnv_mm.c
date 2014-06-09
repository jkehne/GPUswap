#include "nouveau_drv.h"
#include "pscnv_mm.h"
#ifdef __linux__
#include <asm/div64.h>
#endif

#undef PSCNV_RB_AUGMENT

#define GTYPES 4
#define TMASK 3
#define LTMASK 1

static inline uint64_t
pscnv_rounddown (uint64_t x, uint32_t y)
{
#ifdef __linux__
	do_div(x, y);
#else
	x /= y;
#endif
	x *= y;
	return x;
}

static inline uint64_t
pscnv_roundup (uint64_t x, uint32_t y)
{
	return pscnv_rounddown(x + y - 1, y);
}

static void PSCNV_RB_AUGMENT(struct pscnv_mm_node *node) {
	int i;
	struct pscnv_mm_node *left = PSCNV_RB_LEFT(node, entry);
	struct pscnv_mm_node *right = PSCNV_RB_RIGHT(node, entry);
	for (i = 0; i < GTYPES; i++) {
		node->maxgap[i] = node->gap[i];
		if (left && left->maxgap[i] > node->maxgap[i])
			node->maxgap[i] = left->maxgap[i];
		if (right && right->maxgap[i] > node->maxgap[i])
			node->maxgap[i] = right->maxgap[i];
	}
}

static int nodecmp(struct pscnv_mm_node *a, struct pscnv_mm_node *b) {
	if (a->sentinel < b->sentinel)
		return -1;
	else if (a->sentinel > b->sentinel)
		return 1;
	if (a->start < b->start)
		return -1;
	else if (a->start > b->start)
		return 1;
	return 0;
}

PSCNV_RB_GENERATE_STATIC(pscnv_mm_head, pscnv_mm_node, entry, nodecmp)

static unsigned pscnv_mm_validate_node(struct pscnv_mm_node *head, const char *msg)
{
	unsigned dl, dr, ret;
	struct pscnv_mm_node *left, *right;
	if (!head)
		return 1;
	left = head->entry.rbe_left;
	right = head->entry.rbe_right;
	dl = pscnv_mm_validate_node(left, msg);
	dr = pscnv_mm_validate_node(right, msg);
	if (dl == ~0 || dr == ~0)
		return ~0;
	ret = dl + (head->entry.rbe_color == PSCNV_RB_BLACK);
	if (dl != dr) {
		if (left && right)
			NV_ERROR(head->mm->dev, "MM: RB Tree violation %s, %llx..%llx left child %llx..%llx (depth %u) has depth differing with right child %llx..%llx (depth %u)\n",
				 msg, head->start, head->start + head->size, left->start, left->start + left->size, dl, right->start, right->start + right->size, dr);
		else if (left)
			NV_ERROR(head->mm->dev, "MM: RB Tree violation %s, %llx..%llx left child %llx..%llx (depth %u) has depth differing with right child NULL (depth 1)\n",
				 msg, head->start, head->start + head->size, left->start, left->start + left->size, dl);
		else if (right)
			NV_ERROR(head->mm->dev, "MM: RB Tree violation %s, %llx..%llx left child NULL (depth 1) has depth differing with right child %llx..%llx (depth %u)\n",
				 msg, head->start, head->start + head->size, right->start, right->start + right->size, dr);
		else
			NV_ERROR(head->mm->dev, "MM: RB Tree violation %s, %llx..%llx with no childs, amazing..\n",
				 msg, head->start, head->start + head->size);
		ret = ~0;
	} else if (head->entry.rbe_color == PSCNV_RB_RED) {
		if (left && left->entry.rbe_color == PSCNV_RB_RED) {
			NV_ERROR(head->mm->dev, "MM: RED RED Tree violation %s, %llx..%llx has left node %llx..%llx in same color!\n",
				 msg, head->start, head->start + head->size, left->start, left->start + left->size);
			ret = ~0;
		}
		if (right && right->entry.rbe_color == PSCNV_RB_RED) {
			NV_ERROR(head->mm->dev, "MM: RED RED Tree violation %s, %llx..%llx has right node %llx..%llx in same color!\n",
				 msg, head->start, head->start + head->size, right->start, right->start + right->size);
			ret = ~0;
		}
	}
	if (left && nodecmp(head, left) != 1) {
		NV_ERROR(head->mm->dev, "MM: Binary Tree violation %s, %llx..%llx has left node %llx..%llx!\n",
			 msg, head->start, head->start + head->size, left->start, left->start + left->size);
		ret = ~0;
	}
	if (right && nodecmp(head, right) != -1) {
		NV_ERROR(head->mm->dev, "MM: Binary Tree violation %s, %llx..%llx has right node %llx..%llx!\n",
			 msg, head->start, head->start + head->size, right->start, right->start + right->size);
		ret = ~0;
	}
	return ret;
}

static void pscnv_mm_dump(struct pscnv_mm_node *head)
{
	struct pscnv_mm_node *left, *right;
	if (!head)
		return;
	left = head->entry.rbe_left;
	right = head->entry.rbe_right;
	pscnv_mm_dump(left);
	if (left && right)
		NV_ERROR(head->mm->dev, "MM: node %llx..%llx left child %llx..%llx and right child %llx..%llx\n",
			 head->start, head->start + head->size, left->start, left->start + left->size, right->start, right->start + right->size);
	else if (left)
		NV_ERROR(head->mm->dev, "MM: node %llx..%llx left child %llx..%llx and right child NULL\n",
			 head->start, head->start + head->size, left->start, left->start + left->size);
	else if (right)
		NV_ERROR(head->mm->dev, "MM: node %llx..%llx left child NULL and right child %llx..%llx\n",
			 head->start, head->start + head->size, right->start, right->start + right->size);
	else
		NV_ERROR(head->mm->dev, "MM: node %llx..%llx left child NULL and right child NULL\n",
			 head->start, head->start + head->size);
	pscnv_mm_dump(right);
}

static void pscnv_mm_validate(struct pscnv_mm *mm, const char *msg)
{
	if (pscnv_mm_debug < 1)
		return;
	if (pscnv_mm_validate_node(PSCNV_RB_ROOT(&mm->head), msg) == ~0)
		pscnv_mm_dump(PSCNV_RB_ROOT(&mm->head));
}

static void pscnv_mm_getfree(struct pscnv_mm_node *node, int type, uint64_t *start, uint64_t *end) {
	uint64_t s = node->start, e = node->start + node->size;
	struct pscnv_mm_node *prev = PSCNV_RB_PREV(pscnv_mm_head, entry, node);
	struct pscnv_mm_node *next = PSCNV_RB_NEXT(pscnv_mm_head, entry, node);
	if (prev->type != (type & LTMASK))
		s = pscnv_roundup(s, node->mm->tssize);
	if (next->type != (type & LTMASK))
		e = pscnv_rounddown(e, node->mm->tssize);
	if (type & PSCNV_MM_LP) {
		s = pscnv_roundup(s, node->mm->lpsize);
		e = pscnv_rounddown(e, node->mm->lpsize);
	} else {
		s = pscnv_roundup(s, node->mm->spsize);
		e = pscnv_rounddown(e, node->mm->spsize);
	}
	if (e < s)
		e = s;
	*start = s;
	*end = e;
}

static void pscnv_mm_augup(struct pscnv_mm_node *node) {
	struct pscnv_mm_node *n;
	for (n = node; n; n = PSCNV_RB_PARENT(n, entry))
		PSCNV_RB_AUGMENT(n);
}

/* free_node operations that do not actually free reserved memory, but just
 * set up the nodes in the tree, should be silent */
static void pscnv_mm_free_node(struct pscnv_mm_node *node, bool silent) {
	struct pscnv_mm_node *prev = PSCNV_RB_PREV(pscnv_mm_head, entry, node);
	struct pscnv_mm_node *next = PSCNV_RB_NEXT(pscnv_mm_head, entry, node);
	int i;
	if (pscnv_mm_debug >= 2 || (!silent && pscnv_mm_debug >= 1))
		NV_INFO(node->mm->dev, "MM: [%s] Freeing node %llx..%llx of type %d\n", node->mm->name, node->start, node->start + node->size, node->type);
	if (node->next) {
		node->next->prev = NULL;
		node->next = NULL;
	}
	if (pscnv_mm_debug >= 1 && node->prev)
		NV_ERROR(node->mm->dev, "A node that's about to be freed should not have a valid prev pointer!\n");
	node->prev = NULL;
	node->type = PSCNV_MM_TYPE_FREE;
	if (prev->type == PSCNV_MM_TYPE_FREE) {
		if (pscnv_mm_debug >= 2)
			NV_INFO(node->mm->dev, "MM: Merging left with node %llx..%llx\n", prev->start, prev->start + prev->size);
		if (prev->start + prev->size != node->start) {
			NV_ERROR(node->mm->dev, "MM: node %llx..%llx not contiguous with prev %llx..%llx\n",
				 node->start, node->start + node->size, prev->start, prev->start + prev->size);
			pscnv_mm_dump(PSCNV_RB_ROOT(&node->mm->head));
		} else {
			node->start = prev->start;
			node->size += prev->size;
			PSCNV_RB_REMOVE(pscnv_mm_head, &node->mm->head, prev);
			kfree(prev);
		}
	}
	if (next->type == PSCNV_MM_TYPE_FREE) {
		if (pscnv_mm_debug >= 2)
			NV_INFO(node->mm->dev, "MM: Merging right with node %llx..%llx\n", next->start, next->start + next->size);
		if (node->start + node->size != next->start) {
			NV_ERROR(node->mm->dev, "MM: node %llx..%llx not contiguous with next %llx..%llx\n",
				 node->start, node->start + node->size, next->start, next->start + next->size);
			pscnv_mm_dump(PSCNV_RB_ROOT(&node->mm->head));
		} else {
			node->size += next->size;
			PSCNV_RB_REMOVE(pscnv_mm_head, &node->mm->head, next);
			kfree(next);
		}
	}
	for (i = 0; i < GTYPES; i++) {
		uint64_t s, e;
		pscnv_mm_getfree(node, i, &s, &e);
		node->gap[i] = e - s;
	}
	pscnv_mm_augup(node);
}

void pscnv_mm_free(struct pscnv_mm_node *node) {
	struct pscnv_mm *mm = node->mm;
	pscnv_mm_validate(mm, "before free");
	if (node->type == PSCNV_MM_TYPE_FREE) {
		NV_ERROR(node->mm->dev, "Freeing already freed node %llx\n", node->start);
		return;
	}
	while (node->prev)
		node = node->prev;
	while (node) {
		struct pscnv_mm_node *next = node->next;
		pscnv_mm_free_node(node, false);
		node = next;
	}
	pscnv_mm_validate(mm, "after free");
}

int pscnv_mm_init(struct drm_device *dev, const char *name, uint64_t start, uint64_t end, uint32_t spsize, uint32_t lpsize, uint32_t tssize, struct pscnv_mm **res) {
	struct pscnv_mm *mm = kzalloc(sizeof *mm, GFP_KERNEL);
	struct pscnv_mm_node *ss, *se, *node;
	if (!mm)
		return -ENOMEM;
	if (!(ss = kzalloc(sizeof *ss, GFP_KERNEL))) {
		kfree(mm);
		return -ENOMEM;
	}
	if (!(se = kzalloc(sizeof *se, GFP_KERNEL))) {
		kfree(ss);
		kfree(mm);
		return -ENOMEM;
	}
	if (!(node = kzalloc(sizeof *node, GFP_KERNEL))) {
		kfree(se);
		kfree(ss);
		kfree(mm);
		return -ENOMEM;
	}
	mm->dev = dev;
	mm->name = name;
	mm->spsize = spsize;
	mm->lpsize = lpsize;
	mm->tssize = tssize;
	ss->type = se->type = PSCNV_MM_TYPE_USED0;
	ss->sentinel = -1;
	se->sentinel = 1;
	ss->start = start;
	se->start = end;
	node->start = start;
	node->size = end - start;
	node->mm = ss->mm = se->mm = mm;
	PSCNV_RB_INSERT(pscnv_mm_head, &mm->head, ss);
	PSCNV_RB_INSERT(pscnv_mm_head, &mm->head, node);
	PSCNV_RB_INSERT(pscnv_mm_head, &mm->head, se);
	pscnv_mm_free_node(node, true);
	pscnv_mm_validate(mm, "after pscnv_mm_init");
	*res = mm;
	return 0;
}

void pscnv_mm_takedown(struct pscnv_mm *mm, void (*free_callback)(struct pscnv_mm_node *)) {
	struct pscnv_mm_node *cur;
	pscnv_mm_validate(mm, "before mm_takedown");
restart:
	cur = PSCNV_RB_MIN(pscnv_mm_head, &mm->head);
	cur = PSCNV_RB_NEXT(pscnv_mm_head, entry, cur);
	while (cur->type == PSCNV_MM_TYPE_FREE)
		cur = PSCNV_RB_NEXT(pscnv_mm_head, entry, cur);
	if (!cur->sentinel) {
		while (cur->prev)
			cur = cur->prev;
		if (pscnv_mm_debug >= 1)
			NV_INFO (mm->dev, "MM: [%s] takedown free %llx..%llx type %d\n", mm->name, cur->start, cur->start + cur->size, cur->type);
		free_callback(cur);
		goto restart;
	}
	while ((cur = PSCNV_RB_ROOT(&mm->head))) {
		PSCNV_RB_REMOVE(pscnv_mm_head, &mm->head, cur);
		kfree(cur);
	}
	kfree(mm);
}

static int pscnv_mm_alloc_single(struct pscnv_mm_node *node, uint64_t size, uint32_t flags, uint64_t start, uint64_t end, struct pscnv_mm_node **res) {
	struct pscnv_mm_node *left = PSCNV_RB_LEFT(node, entry), *right = PSCNV_RB_RIGHT(node, entry);
	uint64_t minsz = ((flags & PSCNV_MM_FRAGOK) ? 1 : size);
	int lok = left && left->maxgap[flags & TMASK] >= minsz && node->start > start;
	int rok = right && right->maxgap[flags & TMASK] >= minsz && node->start + node->size < end;
	int back = flags & PSCNV_MM_FROMBACK;
	uint64_t s, e;
	struct pscnv_mm_node *lsp = 0, *rsp = 0;
	int i;

	if (!back && lok && !pscnv_mm_alloc_single(left, size, flags, start, end, res))
		return 0;
	if (back && rok && !pscnv_mm_alloc_single(right, size, flags, start, end, res))
		return 0;

	if (node->type == PSCNV_MM_TYPE_FREE) {
		pscnv_mm_getfree(node, flags & TMASK, &s, &e);
		if (start > s)
			s = start;
		if (end < e)
			e = end;
		if (e < s)
			e = s;
		if (e-s >= minsz) {
			if (pscnv_mm_debug >= 2)
				NV_INFO(node->mm->dev, "MM: [%s] Using node %llx..%llx, space %llx..%llx\n", node->mm->name, node->start, node->start + node->size, s, e);
			if (e-s > size) {
				if (back)
					s = e - size;
				else
					e = s + size;
			}

			if (s != node->start) {
				lsp = kzalloc(sizeof *lsp, GFP_KERNEL);
				if (!lsp)
					return -ENOMEM;
			}
			if (e != node->start + node->size) {
				rsp = kzalloc(sizeof *rsp, GFP_KERNEL);
				if (!rsp) {
					if (lsp)
						kfree(lsp);
					return -ENOMEM;
				}
			}

			node->type = flags & LTMASK;
			for (i = 0; i < GTYPES; i++)
				node->gap[i] = 0;
			pscnv_mm_augup(node);

			if (lsp) {
				lsp->mm = node->mm;
				lsp->start = node->start;
				lsp->size = s - node->start;
				node->size -= lsp->size;
				node->start = s;
				PSCNV_RB_INSERT(pscnv_mm_head, &node->mm->head, lsp);
				pscnv_mm_free_node(lsp, true);
			}

			if (rsp) {
				rsp->mm = node->mm;
				rsp->start = e;
				rsp->size = node->start + node->size - e;
				node->size -= rsp->size;
				PSCNV_RB_INSERT(pscnv_mm_head, &node->mm->head, rsp);
				pscnv_mm_free_node(rsp, true);
			}
			if (pscnv_mm_debug >= 2)
				NV_INFO(node->mm->dev, "MM: After split: %llx..%llx\n", node->start, node->start + node->size);

			*res = node;
			return 0;
		}
	}

	if (back && lok && !pscnv_mm_alloc_single(left, size, flags, start, end, res))
		return 0;
	if (!back && rok && !pscnv_mm_alloc_single(right, size, flags, start, end, res))
		return 0;
	return -ENOMEM;
}

int pscnv_mm_alloc(struct pscnv_mm *mm, uint64_t size, uint32_t flags, uint64_t start, uint64_t end, struct pscnv_mm_node **res) {
	uint32_t psize;
	struct pscnv_mm_node *last = 0;
	int ret;
	if (flags & PSCNV_MM_LP)
		psize = mm->lpsize;
	else
		psize = mm->spsize;
	size = pscnv_roundup(size, psize);
	start = pscnv_roundup(start, psize);
	end = pscnv_rounddown(end, psize);
	/* avoid various bounduary conditions */
	if (size > (1ull << 60))
		return -EINVAL;
	if (pscnv_mm_debug >= 2)
		NV_INFO(mm->dev, "MM: [%s] Request for size %llx at %llx..%llx flags %d\n", mm->name, size, start, end, flags);
	pscnv_mm_validate(mm, "before mm_alloc");
	while (size) {
		struct pscnv_mm_node *cur;
		ret = pscnv_mm_alloc_single(PSCNV_RB_ROOT(&mm->head), size, flags, start, end, &cur);
		if (ret) {
			while (last) {
				cur = last->prev;
				pscnv_mm_free_node(last, true);
				last = cur;
			}
			return ret;
		}
		if (pscnv_mm_debug >= 1)
			NV_INFO(mm->dev, "MM: [%s] Allocated size %llx at %llx-%llx\n", mm->name, cur->size, cur->start, cur->start+cur->size);
		
		size -= cur->size;
		if (last) {
			last->next = cur;
			cur->prev = last;
			last = cur;
		} else {
			*res = last = cur;
			cur->prev = 0;
		}
	}
	if (last)
		last->next = 0;
	pscnv_mm_validate(mm, "after mm_alloc");
	return 0;
}

struct pscnv_mm_node *pscnv_mm_find_node(struct pscnv_mm *mm, uint64_t addr) {
	struct pscnv_mm_node *node = PSCNV_RB_ROOT(&mm->head);
	while (node) {
		if (addr < node->start)
			node = PSCNV_RB_LEFT(node, entry);
		else if (addr >= node->start + node->size)
			node = PSCNV_RB_RIGHT(node, entry);
		else
			return node;
	}
	return 0;
}
