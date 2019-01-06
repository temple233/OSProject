#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/vfs_radix_tree.h>

#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <zjunix/utils.h>
#include <driver/vga.h>

/**
 * 32/6 + 2 == 7
 * 0   -> root
 *
 * 1   -> node
 * ... -> node
 * 2^(6*i) - 1 maxindex
 * 
 * [6] == all 32bist address index => a BUFFENTRY / index
 */
static unsigned long depth_to_maxindex[RADIX_TREE_MAX_PATH];


static int radix_tree_extend(struct radix_tree_root *root, unsigned long index)
{

}

static void
radix_tree_node_ctor(void *node, kmem_cache_t *cachep, unsigned long flags)
{
	memset(node, 0, sizeof(struct radix_tree_node));
}

static __init unsigned long __maxindex(unsigned int height)
{
	unsigned int tmp = height * RADIX_TREE_MAP_SHIFT;
	unsigned long index = (~0UL >> (RADIX_TREE_INDEX_BITS - tmp - 1)) >> 1;

	if (tmp >= RADIX_TREE_INDEX_BITS)
		index = ~0UL;
	return index;
}

static __init void radix_tree_init_maxindex(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(height_to_maxindex); i++)
		height_to_maxindex[i] = __maxindex(i);
}