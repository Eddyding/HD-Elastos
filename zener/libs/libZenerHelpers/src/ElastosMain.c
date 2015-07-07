/*
 * Copyright 2015, Tongji Operating System Group & elastos.org
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 */

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <allocman/vka.h>
#include <allocman/bootstrap.h>

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4utils/util.h>
#include <sel4utils/mapping.h>
#include <sel4utils/vspace.h>

#include <sel4test/test.h>

#include <vka/capops.h>

#include "helpers.h"
#include "test.h"
#include "ComputeEnv.h"

/* dummy global for libsel4muslcsys */
char _cpio_archive[1];

// endpoint to call back to the ElastosLocalhost
static seL4_CPtr endpoint;

/* global static memory for init */
static sel4utils_alloc_data_t alloc_data;

/* dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE ((1 << seL4_PageBits) * 200)

/* allocator static pool */
#define ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 10)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];

/* override abort, called by exit (and assert fail) */
void abort(void)
{
    /* send back a failure */
    seL4_MessageInfo_t info = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 1);
    seL4_SetMR(0, -1);
    seL4_Send(endpoint, info);

    assert(0);
    while (1);
}

static COMPUTE_ENV_DATA *receive_init_data(seL4_CPtr endpoint)
{
    /* wait for a message */
    seL4_Word badge;
    UNUSED seL4_MessageInfo_t info;

    info = seL4_Wait(endpoint, &badge);

    /* check the label is correct */
    assert(seL4_MessageInfo_get_label(info) == seL4_NoFault);
    assert(seL4_MessageInfo_get_length(info) == 1);

    COMPUTE_ENV_DATA *init_data = (COMPUTE_ENV_DATA *) seL4_GetMR(0);
    assert(init_data->free_slots.start != 0);
    assert(init_data->free_slots.end != 0);

    return init_data;
}

static void init_allocator(env_t env, COMPUTE_ENV_DATA *init_data)
{
    UNUSED int error;
    UNUSED reservation_t virtual_reservation;

    /* initialise allocator */
    allocman_t *allocator = bootstrap_use_current_1level(init_data->root_cnode,
                                                         init_data->cspace_size_bits, init_data->free_slots.start,
                                                         init_data->free_slots.end, ALLOCATOR_STATIC_POOL_SIZE,
                                                         allocator_mem_pool);
    assert(allocator != NULL);

    allocman_make_vka(&env->vka, allocator);

    /* fill the allocator with untypeds */
    int slot, size_bits_index;
    for (slot = init_data->untypeds.start, size_bits_index = 0;
            slot <= init_data->untypeds.end;
            slot++, size_bits_index++) {

        cspacepath_t path;
        vka_cspace_make_path(&env->vka, slot, &path);
        /* allocman doesn't require the paddr unless we need to ask for phys addresses,
         * which we don't. */
        uint32_t fake_paddr = 0;
        uint32_t size_bits = init_data->untyped_size_bits_list[size_bits_index];
        error = allocman_utspace_add_uts(allocator, 1, &path, &size_bits, &fake_paddr);
        assert(!error);
    }

    /* create a vspace */
    void *existing_frames[] = { init_data, seL4_GetIPCBuffer()};
    error = sel4utils_bootstrap_vspace(&env->vspace, &alloc_data, init_data->page_directory, &env->vka, NULL, NULL, existing_frames);

    /* switch the allocator to a virtual memory pool */
    void *vaddr;
    virtual_reservation = vspace_reserve_range(&env->vspace, ALLOCATOR_VIRTUAL_POOL_SIZE,
                                               seL4_AllRights, 1, &vaddr);
    assert(virtual_reservation.res);
    bootstrap_configure_virtual_pool(allocator, vaddr, ALLOCATOR_VIRTUAL_POOL_SIZE,
                                     env->page_directory);

}


int main(int argc, char **argv)
{

    COMPUTE_ENV_DATA *init_data;
    struct env env;

    assert(argc >= 2);
    /* in order to have some shitty almost-fork-like semantics
     * main can get run multiple times. Look in src/helpers.c
     * for where this is used. Just means we check the first
     * arg, and if not NULL jmp to it */
    void (*helper_thread)(int argc,char **argv) = (void(*)(int, char**))atoi(argv[1]);
    if (helper_thread) {
        helper_thread(argc, argv);
    }

    /* parse args */
    assert(argc == 3);
    endpoint = (seL4_CPtr) atoi(argv[2]);

    /* read in init data */
    init_data = receive_init_data(endpoint);

    /* configure env */
    env.cspace_root = init_data->root_cnode;
    env.page_directory = init_data->page_directory;
    env.endpoint = endpoint;
    env.priority = init_data->priority;
    env.cspace_size_bits = init_data->cspace_size_bits;
    env.tcb = init_data->tcb;
    env.domain = init_data->domain;
#ifndef CONFIG_KERNEL_STABLE
    env.asid_pool = init_data->asid_pool;
#endif /* CONFIG_KERNEL_STABLE */
#ifdef CONFIG_IOMMU
    env.io_space = init_data->io_space;
#endif
    env.num_regions = init_data->num_elf_regions;
    memcpy(env.regions, init_data->elf_regions, sizeof(sel4utils_elf_region_t) * env.num_regions);

    /* initialse cspace, vspace and untyped memory allocation */
    init_allocator(&env, init_data);

    /* find the app */
    ELASTOS_APPLICATION *app = ElastosMain_get_Elastos_applications(init_data->name);

    /* run the app */
    int result = 0;
    if (app) {
        printf("Running ELASTOS_APPLICATION %s (%s)\n", app->name, app->description);
        result = app->function(&env, app->args);
    } else {
        result = FAILURE;
        LOG_ERROR("Cannot find app %s\n", init_data->name);
    }

    printf("app %s %s\n", init_data->name, result == SUCCESS ? "passed" : "failed");
    /* send our result back */
    seL4_MessageInfo_t info = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 1);
    seL4_SetMR(0, result);
    seL4_Send(endpoint, info);

    /* It is expected that we are torn down by the ElastosLocalhost before we are
     * scheduled to run again after signalling them with the above send.
     */
    assert(!"unreachable");
    return 0;
}

