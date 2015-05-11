/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#ifndef _SYNC_ATOMIC_H_
#define _SYNC_ATOMIC_H_

/** \brief Atomically increment an integer, accounting for possible overflow.
 *
 * @param x Pointer to integer to increment.
 * @param[out] oldval Previous value of the integer. May be written to even if
 *   the increment fails.
 * @return 0 if the increment succeeds, non-zero if it would cause an overflow.
 */
int sync_atomic_increment_safe(volatile int *x, int *oldval);

/** \brief Atomically decrement an integer, accounting for possible overflow.
 *
 * @param x Pointer to integer to decrement.
 * @param[out] oldval Previous value of the integer. May be written to even if
 *   the decrement fails.
 * @return 0 if the decrement succeeds, non-zero if it would cause an overflow.
 */
int sync_atomic_decrement_safe(volatile int *x, int *oldval);

/* Atomically increment an integer and return its new value. */
int sync_atomic_increment(volatile int *x);

/* Atomically decrement an integer and return its new value. */
int sync_atomic_decrement(volatile int *x);

/* Atomic CAS. Returns the old value. */
int sync_atomic_compare_and_swap(volatile int *x, int oldval, int newval);

#endif
