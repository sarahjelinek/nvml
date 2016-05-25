/*
 * Copyright 2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * obj_tx_callbacks.c -- unit test for transaction stage callbacks
 */
#include "unittest.h"

#define	LAYOUT_NAME "tx_callback"

POBJ_LAYOUT_BEGIN(tx_callback);
POBJ_LAYOUT_ROOT(tx_callback, struct pmem_root);
POBJ_LAYOUT_TOID(tx_callback, struct pmem_obj);
POBJ_LAYOUT_END(tx_callback);

struct runtime_info {
	int something;
};

struct pmem_obj {
	struct runtime_info *rt;
	int pmem_info;
};

struct pmem_root {
	TOID(struct pmem_obj) obj;
};

static int freed = 0;

static void
rt_onabort(void *ctx, void *arg)
{
	struct runtime_info *rt = arg;
	UT_OUT("rt_onabort: free");
	free(rt);
	freed++;
}

static void
allocate_pmem(PMEMobjpool *pop, TOID(struct pmem_root) root, int val)
{
	TOID(struct pmem_obj) obj = TX_NEW(struct pmem_obj);
	D_RW(obj)->pmem_info = val;
	D_RW(obj)->rt = malloc(sizeof(struct runtime_info));
	pmemobj_tx_stage_callback_push_front(TX_STAGE_ONABORT, rt_onabort,
			D_RW(obj)->rt);
	D_RW(obj)->rt->something = val;
	TX_ADD_FIELD(root, obj);
	D_RW(root)->obj = obj;
}

static void
do_something_fishy(PMEMobjpool *pop, TOID(struct pmem_root) root)
{
	(void) TX_ALLOC(struct pmem_obj, 1 << 30);
}

static void
rt_oncommit(void *ctx, void *arg)
{
	struct runtime_info *rt = arg;
	UT_OUT("rt_oncommit: free");
	free(rt);
	freed++;
}

static void
free_pmem(PMEMobjpool *pop, TOID(struct pmem_root) root)
{
	TOID(struct pmem_obj) obj = D_RW(root)->obj;
	pmemobj_tx_stage_callback_push_back(TX_STAGE_ONCOMMIT, rt_oncommit,
			D_RW(obj)->rt);
	TX_FREE(obj);
	TX_SET(root, obj, TOID_NULL(struct pmem_obj));
}

static void
test(PMEMobjpool *pop, TOID(struct pmem_root) root)
{
	TX_BEGIN(pop) {
		TX_SET_BASIC_CALLBACK(NULL);
		allocate_pmem(pop, root, 7);
		do_something_fishy(pop, root);
		UT_ASSERT(0);
	} TX_ONCOMMIT {
		UT_ASSERT(0);
	} TX_ONABORT {
		UT_OUT("on abort 1");
	} TX_END

	UT_ASSERTeq(freed, 1);
	freed = 0;

	TX_BEGIN(pop) {
		TX_SET_BASIC_CALLBACK(NULL);
		allocate_pmem(pop, root, 7);
	} TX_ONCOMMIT {
		UT_OUT("on commit 2");
	} TX_ONABORT {
		UT_ASSERT(0);
	} TX_END

	UT_ASSERTeq(freed, 0);

	TX_BEGIN(pop) {
		TX_SET_BASIC_CALLBACK(NULL);
		free_pmem(pop, root);
		do_something_fishy(pop, root);
		UT_ASSERT(0);
	} TX_ONCOMMIT {
		UT_ASSERT(0);
	} TX_ONABORT {
		UT_OUT("on abort 3");
	} TX_END

	UT_ASSERTeq(freed, 0);

	TX_BEGIN(pop) {
		TX_SET_BASIC_CALLBACK(NULL);
		free_pmem(pop, root);
	} TX_ONCOMMIT {
		UT_OUT("on commit 4");
	} TX_ONABORT {
		UT_ASSERT(0);
	} TX_END

	UT_ASSERTeq(freed, 1);
	freed = 0;
}

int
main(int argc, char *argv[])
{
	START(argc, argv, "obj_tx_callbacks");

	if (argc != 2)
		UT_FATAL("usage: %s [file]", argv[0]);

	PMEMobjpool *pop = pmemobj_create(argv[1], LAYOUT_NAME,
			PMEMOBJ_MIN_POOL, S_IWUSR | S_IRUSR);
	if (!pop)
		UT_FATAL("!pmemobj_create");

	TOID(struct pmem_root) root = POBJ_ROOT(pop, struct pmem_root);
	test(pop, root);


	pmemobj_close(pop);

	DONE(NULL);
}
