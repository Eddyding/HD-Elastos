/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <sync/atomic.h>

int sync_atomic_increment(volatile int *x) {
    return __sync_add_and_fetch(x, 1);
}

int sync_atomic_decrement(volatile int *x) {
    return __sync_sub_and_fetch(x, 1);
}

int sync_atomic_compare_and_swap(volatile int *x, int oldval, int newval) {
    return __sync_val_compare_and_swap(x, oldval, newval);
}

int sync_atomic_increment_safe(volatile int *x, int *oldval) {
    assert(x != NULL);
    assert(oldval != NULL);
    do {
        *oldval = *x;
        if (*oldval == INT_MAX) {
            /* We would overflow */
            return -1;
        }
    } while (*oldval != sync_atomic_compare_and_swap(x, *oldval, *oldval + 1));
    return 0;
}

int sync_atomic_decrement_safe(volatile int *x, int *oldval) {
    assert(x != NULL);
    assert(oldval != NULL);
    do {
        *oldval = *x;
        if (*oldval == INT_MIN) {
            /* We would overflow */
            return -1;
        }
    } while (*oldval != sync_atomic_compare_and_swap(x, *oldval, *oldval - 1));
    return 0;
}
