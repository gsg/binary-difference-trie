/*
 * Unordered integer set operations built on difference tries.
 *
 * Copyright 2013 Geoff Gole <geoffgole@gmail.com>
 *
 * This set implementation trades away some of the flexibility and
 * nice properties of more a common data structure like a red-black
 * tree in favour of speed, while retaining reasonable worst-case
 * bounds. The usual insert/find/remove operations are O(W), where W
 * is the number of bits in an int).
 *
 * Memory exhaustion on insert is "handled" by termination.
 *
 * Usage:
 *
 * intset set; // declare
 *
 * intset_init(&set); // initialise
 *
 * // manipulate 
 * intset_insert(&set, 42);
 * assert(intset_contains(&set, 42));
 * intset_remove(&set);
 * assert(intset_size(&set) == 0);
 *
 * intset_destroy(&set); // destroy
 */

#ifndef INTSET_H_
#define INTSET_H_

#include <stdint.h>

struct intset_leaf;
struct intset_branch;

typedef union {
    uintptr_t value;
    struct intset_leaf *leaf;
    struct intset_branch *branch;
} tagged_ptr;

/*
 * The set data structure.
 */
typedef struct {
    tagged_ptr root;
} intset;

/* implementation junk */
void intset_destroy1(tagged_ptr ptr);
unsigned intset_size1(tagged_ptr);
int intset_contains1(tagged_ptr node, unsigned elt);
void intset_insert1(tagged_ptr, tagged_ptr *, unsigned);
void intset_remove1(tagged_ptr, tagged_ptr *, unsigned);

/*
 * Initialise a set. O(1).
 */
static inline void intset_init(intset *s) {
    s->root.value = 0;
}

/*
 * Destroy @set, freeing any memory associated with it. O(n).
 *
 * Operations should not be applied to a destroyed set unless it is
 * initialised again first.
 */
static inline void intset_destroy(intset *set) {
    intset_destroy1(set->root);
}

/*
 * Insert @elt into @set. Does nothing if @elt is already a member of
 * @set. O(W).
 */
static inline void intset_insert(intset *set, unsigned elt) {
    intset_insert1(set->root, &set->root, elt);
}

/*
 * Return the number of elements in @set. O(n).
 */
static inline unsigned intset_size(const intset *set) {
    return intset_size1(set->root);
}

/*
 * Return whether @elt is a member of @set. O(W).
 */
static inline int intset_contains(const intset *set, unsigned elt) {
    return intset_contains1(set->root, elt);
}

/*
 * Remove @elt from @set. Does nothing if @elt is not a member of
 * @set. O(W).
 */
static inline void intset_remove(intset *set, unsigned elt) {
    intset_remove1(set->root, &set->root, elt);
}

#endif
