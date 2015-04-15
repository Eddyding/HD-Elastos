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
#include <sel4/sel4.h>
#include <stddef.h>
#include <sync/atomic.h>
#include <sync/bin_sem_bare.h>

int sync_bin_sem_bare_wait(seL4_CPtr aep, volatile int *value) {
    int val = sync_atomic_decrement(value);
    if (val < 0) {
        seL4_Wait(aep, NULL);
    }
    __sync_synchronize();
    return 0;
}

int sync_bin_sem_bare_post(seL4_CPtr aep, volatile int *value) {
    __sync_synchronize();
    int val = sync_atomic_increment(value);
    assert(*value <= 1);
    if (val <= 0) {
        seL4_Notify(aep, 0);
    }
    return 0;
}
