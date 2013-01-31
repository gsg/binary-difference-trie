/*
 * Difference tries are a variant of tries which attempt to mitigate
 * the shortcomings of tries at the cost of some of their nice
 * properties (mostly, ordering).
 *
 * The basic technique is to extend branch nodes with a mask
 * indicating which bits of a number are to be discriminated upon at
 * that branch: the term "difference tries" arises because each bit in
 * a mask indicates the position of a *difference* between the
 * children of the branch.
 *
 * Leaves are maintained as sorted vectors of numbers. Once reaching a
 * certain length, leaves are split into a branch and a number
 * of leaves.
 * 
 */

/*
 * todo: testing, benchmarking
 * todo: implement coalescing, union, intersection, difference
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "intset.h"

typedef enum { INTSET_BRANCH = 1, INTSET_LEAF } intset_tag;

enum {
    LEAF_SIZE_THRESHOLD = 64,
    BRANCH_BITS = 5,
    BRANCH_LEN = 32,
    TAG_BITS_MASK = 3
};

typedef struct intset_leaf {
    unsigned len;
    unsigned values[1]; /* struct hack */
} intset_leaf;

typedef struct intset_branch {
    unsigned mask;
    tagged_ptr ptrs[BRANCH_LEN];
} intset_branch;

static int is_null(tagged_ptr ptr) {
    return ptr.value == 0;
}

static tagged_ptr null_tagged_ptr() {
    tagged_ptr ptr;
    ptr.value = 0;
    return ptr;
}

static intset_tag tag_of(tagged_ptr ptr) {
    return ptr.value & TAG_BITS_MASK;
}

static intset_leaf *unbox_as_leaf(tagged_ptr ptr) {
    return (intset_leaf *)(ptr.value & ~TAG_BITS_MASK);
}

static intset_branch *unbox_as_branch(tagged_ptr ptr) {
    return (intset_branch *)(ptr.value & ~TAG_BITS_MASK);
}

static tagged_ptr box_as_leaf(intset_leaf *leaf) {
    tagged_ptr ptr;
    ptr.leaf = (intset_leaf *)((uintptr_t)leaf | INTSET_LEAF);
    return ptr;
}

static tagged_ptr box_as_branch(intset_branch *branch) {
    tagged_ptr ptr;
    ptr.branch = (intset_branch *)((uintptr_t)branch | INTSET_BRANCH);
    return ptr;
}

static unsigned leaf_size(unsigned num_elts) {
    return sizeof(intset_leaf) + (num_elts - 1) * sizeof(unsigned);
}

void intset_destroy1(tagged_ptr ptr) {
    if (is_null(ptr))
        return;
    if (tag_of(ptr) == INTSET_LEAF)
        free(unbox_as_leaf(ptr));
    else {
        intset_branch *branch = unbox_as_branch(ptr);
        unsigned i;

        for (i = 0; i < BRANCH_LEN; i++)
            intset_destroy1(branch->ptrs[i]);
        free(branch);
    }
}

static unsigned is_pow2(unsigned n) {
    return (n & (n - 1)) == 0;
}

static void oom_die() {
    fprintf(stderr, "out of memory");
    abort();
}

static intset_leaf *new_leaf(unsigned elt) {
    intset_leaf *leaf = malloc(sizeof(intset_leaf));
    if (leaf == NULL)
        oom_die();
    leaf->len = 1;
    leaf->values[0] = elt;
    return leaf;
}

static unsigned lowest_bit(unsigned x) {
    return x & -x;
}

/*
 * Return a mask of bits that differ in at least one element of
 * @a. The number of bits will be BRANCH_BITS exactly.
 * 
 * Why doesn't this doesn't run over the end of the array? Because the
 * invariants of the data structure mean that the right number of bits
 * will always be found before then.
 */
static unsigned differing_bits(unsigned *a) {
    unsigned base = *a, bits = 0;
    unsigned bits_left = BRANCH_BITS;

    while (bits_left > 0) {
        unsigned diff;
        diff = (base ^ *a++) & ~bits;
        if (diff) {
            bits |= lowest_bit(diff);
            bits_left--;
        }
    }
    return bits;
}

static intset_branch *new_branch(unsigned mask) {
    intset_branch *branch = calloc(1, sizeof(intset_branch));
    if (branch == NULL)
        oom_die();
    branch->mask = mask;
    return branch;
}

/*
 * Return the index of @x in a branch with mask @mask.
 */
static unsigned branch_index(unsigned mask, unsigned x) {
    unsigned index = 0, n = 0, num_bits = BRANCH_BITS;

    while (num_bits--) {
        unsigned bit = lowest_bit(mask);
        mask ^= bit; /* works because bit is a subset of mask */
        index |= !!(bit & x) << n;
        n++;
    }

    return index;
}

/*
 * Specialised insertion routine that assumes the pointer argument is
 * either null or an leaf in which all the elements are less than the
 * insertion value.
 */
static tagged_ptr insert_ordered(tagged_ptr ptr, unsigned elt) {
    if (is_null(ptr))
        return box_as_leaf(new_leaf(elt));
    else {
        intset_leaf *leaf = unbox_as_leaf(ptr);

        if (is_pow2(leaf->len)) {
            leaf = realloc(leaf, leaf_size(leaf->len * 2));
            if (leaf == NULL)
                oom_die();
        }
        leaf->values[leaf->len++] = elt;

        return box_as_leaf(leaf);
    }
}

static tagged_ptr split_leaf_insert(intset_leaf *leaf, unsigned elt) {
    unsigned i, index;
    intset_branch *branch = new_branch(differing_bits(leaf->values));

    for (i = 0; i < leaf->len; i++) {
        index = branch_index(branch->mask, leaf->values[i]);
        branch->ptrs[index] = insert_ordered(branch->ptrs[index],
                                             leaf->values[i]);
    }

    index = branch_index(branch->mask, elt);
    intset_insert1(branch->ptrs[index], &branch->ptrs[index], elt);
    free(leaf);

    return box_as_branch(branch);
}

/* todo: might be worthwhile trying a branchless power-of-two
 * search */
#if 0
static unsigned
find_in_block(const unsigned a[], unsigned len, unsigned elt) {
    unsigned diff, i, l = 0, r = len;

    while (1) {
        unsigned mid;

        diff = r - l;
        if (diff < 32)
            break;
        mid = l + diff / 2;
        if (elt < a[mid])
            r = mid;
        else if (elt > a[mid])
            l = mid + 1;
        else
            return mid;
    }
    for (i = l; i < r; i++)
        if (!(a[i] < elt))
            break;
    return i;
}
#elif 1
static unsigned
find_in_block(const unsigned a[], unsigned len, unsigned elt) {
    unsigned i;
    for (i = 0; i < len; i++)
        if (!(a[i] < elt))
            break;
    return i;
}
#endif

static tagged_ptr insert_in_leaf(intset_leaf *leaf, unsigned elt) {
    unsigned len = leaf->len;
    unsigned i, point = find_in_block(leaf->values, len, elt);

    if (point < len && leaf->values[point] == elt)
        return box_as_leaf(leaf);
    if (is_pow2(len)) {
        if (len == LEAF_SIZE_THRESHOLD)
            return split_leaf_insert(leaf, elt);
        leaf = realloc(leaf, leaf_size(len * 2));
        if (leaf == NULL)
            oom_die();
    }
    for (i = len; i > point; i--)
        leaf->values[i] = leaf->values[i - 1];
    leaf->values[point] = elt;
    leaf->len = len + 1;
    return box_as_leaf(leaf);
}

void intset_insert1(tagged_ptr node, tagged_ptr *ref, unsigned elt) {
    unsigned index;
    intset_branch *branch;

    while (1) {
        if (is_null(node)) {
            *ref = box_as_leaf(new_leaf(elt));
            return;
        }
        if (tag_of(node) == INTSET_LEAF) {
            *ref = insert_in_leaf(unbox_as_leaf(node), elt);
            return;
        }
        branch = unbox_as_branch(node);
        index = branch_index(branch->mask, elt);
        ref = &branch->ptrs[index];
        node = branch->ptrs[index];
    }
}

unsigned intset_size1(tagged_ptr node) {
    const intset_branch *branch;
    unsigned i, count = 0;

    if (is_null(node))
        return 0;
    if (tag_of(node) == INTSET_LEAF) {
        intset_leaf *leaf = unbox_as_leaf(node);
        return leaf->len;
    }
    
    branch = unbox_as_branch(node);
    for (i = 0; i < BRANCH_LEN; i++)
        count += intset_size1(branch->ptrs[i]);
    return count;
}

int intset_contains1(tagged_ptr node, unsigned elt) {
    while (1) {
        if (is_null(node))
            return 0;
        if (tag_of(node) == INTSET_LEAF) {
            const intset_leaf *leaf = unbox_as_leaf(node);
            unsigned i = find_in_block(leaf->values, leaf->len, elt);
            return i < leaf->len && leaf->values[i] == elt;
        }
        else {
            const intset_branch *branch = unbox_as_branch(node);
            unsigned i = branch_index(branch->mask, elt);
            node = branch->ptrs[i];
        }
    }
}

static tagged_ptr remove_in_leaf(intset_leaf *leaf, unsigned elt) {
    unsigned i, point = find_in_block(leaf->values, leaf->len, elt);

    if (point == leaf->len || leaf->values[point] != elt)
        return box_as_leaf(leaf);

    if (leaf->len == 1) {
        free(leaf);
        return null_tagged_ptr();
    }

    for (i = point; i < leaf->len - 1; i++)
        leaf->values[i] = leaf->values[i + 1];
    leaf->len--;
    return box_as_leaf(leaf);
}

/*
 * fixme: remove branches that become empty
 */
void intset_remove1(tagged_ptr node, tagged_ptr *ref, unsigned elt) {
    while (1) {
        if (is_null(node))
            return;
        if (tag_of(node) == INTSET_LEAF) {
            *ref = remove_in_leaf(unbox_as_leaf(node), elt);
            return;
        }
        else {
            intset_branch *branch = unbox_as_branch(node);
            unsigned i = branch_index(branch->mask, elt);
            node = branch->ptrs[i];
            ref = &branch->ptrs[i];
        }
    }
}
