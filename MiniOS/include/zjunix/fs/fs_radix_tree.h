#ifndef _ZJUNIX_VFS_RADIX_TREE_H
#define _ZJUNIX_VFS_RADIX_TREE_H


/**
 * 32 bit address
 * 64 aray tree
 * height == 6
 * bits{6, 6, 6, 6, 6, 2} = 32bits
 * 
 * tags[0] => 64bits for subtree write back/dirty
 * tags[1] => ditto
 */
#define RADIX_TREE_MAP_SHIFT    6
#define RADIX_TREE_TAGS         2

#define RADIX_TREE_MAP_SIZE (1UL << RADIX_TREE_MAP_SHIFT)
#define RADIX_TREE_MAP_MASK (RADIX_TREE_MAP_SIZE-1)

#define RADIX_TREE_TAG_LONGS    \
    ((RADIX_TREE_MAP_SIZE + BITS_PER_LONG - 1) / BITS_PER_LONG)

struct radix_tree_node {
    unsigned int    count;
    void        *slots[RADIX_TREE_MAP_SIZE];
    unsigned long   tags[RADIX_TREE_TAGS][RADIX_TREE_TAG_LONGS];
};

struct radix_tree_path {
    struct radix_tree_node *node, **slot;
    int offset;
};

struct radix_tree_root {
    unsigned int height;
    struct radix_tree_node *rnode;
};

#define RADIX_TREE_INDEX_BITS  (BITS_PER_BYTE * sizeof(unsigned long))
#define RADIX_TREE_MAX_PATH (RADIX_TREE_INDEX_BITS/RADIX_TREE_MAP_SHIFT + 2)


int radix_tree_insert(struct radix_tree_root *root,
			unsigned long index, void *item);

/**
 *	radix_tree_lookup    -    perform lookup operation on a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Lookup the item at the position @index in the radix tree @root.
 */
void *radix_tree_lookup(struct radix_tree_root *root, unsigned long index);


/**
 *	radix_tree_tag_set - set a tag on a radix tree node
 *	@root:		radix tree root
 *	@index:		index key
 *	@tag: 		tag index
 *
 *	Set the search tag corresponging to @index in the radix tree.  From
 *	the root all the way down to the leaf node.
 *
 *	Returns the address of the tagged item.   Setting a tag on a not-present
 *	item is a bug.
 */
void *radix_tree_tag_set(struct radix_tree_root *root,
			unsigned long index, int tag);

/**
 *	radix_tree_tag_clear - clear a tag on a radix tree node
 *	@root:		radix tree root
 *	@index:		index key
 *	@tag: 		tag index
 *
 *	Clear the search tag corresponging to @index in the radix tree.  If
 *	this causes the leaf node to have no tags set then clear the tag in the
 *	next-to-leaf node, etc.
 *
 *	Returns the address of the tagged item on success, else NULL.  ie:
 *	has the same return value and semantics as radix_tree_lookup().
 */
void *radix_tree_tag_clear(struct radix_tree_root *root,
			unsigned long index, int tag);

int radix_tree_tag_get(struct radix_tree_root *root,
			unsigned long index, int tag);

void radix_tree_init(void);
#endif