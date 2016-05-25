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
 * inode.c -- inode operations
 */

#include <errno.h>
#include <stdlib.h>

#include "internal.h"
#include "util.h"
#include "valgrind_internal.h"

/*
 * pmfi_path -- returns one of the full paths inode can be reached on
 *
 * Only for debugging.
 */
const char *
pmfi_path(TOID(struct pmemfile_inode) tinode)
{
#ifdef	DEBUG
	struct pmemfile_inode *inode = D_RW(tinode);
	if (!inode) {
		LOG(LTRC, "0x%lx: no inode", tinode.oid.off);
		return NULL;
	}
	struct pmemfile_inode_rt *rt = (void *)inode->rt.data;
	if (!rt) {
		LOG(LTRC, "0x%lx: no rt", tinode.oid.off);
		return NULL;
	}
	if (!rt->path)
		LOG(LTRC, "0x%lx: no rt->path", tinode.oid.off);
	return rt->path;
#else
	return NULL;
#endif
}

/*
 * pmemfile_inode_ref -- increases inode runtime reference counter
 *
 * Does not need transaction.
 */
struct pmemfile_inode_rt *
pmemfile_inode_ref(PMEMfilepool *pfp, TOID(struct pmemfile_inode) inode)
{
	struct pmemfile_inode_rt *rt = _pmemfile_inode_get(pfp, inode, true);
	ASSERTne(rt, NULL);

	LOG(LDBG, "inode 0x%lx path %s", inode.oid.off, pmfi_path(inode));

	return rt;
}

/*
 * pmemfile_inode_ref_set_path -- increases inode runtime reference counter and
 * sets (debug) path on it
 *
 * Does not need transaction.
 */
struct pmemfile_inode_rt *
pmemfile_inode_ref_set_path(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) inode,
		TOID(struct pmemfile_inode) parent_inode,
		const char *name)
{
	struct pmemfile_inode_rt *rt = _pmemfile_inode_get(pfp, inode, true);
	ASSERTne(rt, NULL);
	pmemfile_set_path_debug(pfp, parent_inode, inode, name);

	LOG(LDBG, "inode 0x%lx path %s", inode.oid.off, pmfi_path(inode));

	return rt;
}

struct unlock_params {
	volatile uint64_t *run_id;
	uint64_t old_run_id;
	uint64_t new_run_id;
};

/*
 * file_unlock_inode_now -- (internal) unlocks inode
 */
static void
file_unlock_inode_now(volatile uint64_t *run_id, uint64_t old_run_id,
		uint64_t new_run_id)
{
	VALGRIND_MUTEX_UNLOCK_PRE(run_id);
#ifdef	DEBUG
	if (!__sync_bool_compare_and_swap(run_id, old_run_id, new_run_id))
		FATAL("failure to unlock inode");
#else
	__atomic_store_n(run_id, new_run_id, __ATOMIC_SEQ_CST);
#endif
	VALGRIND_MUTEX_UNLOCK_POST(run_id);
}

/*
 * file_unlock_inode -- (internal) unlocks inode
 */
static void
file_unlock_inode(void *ctx, void *arg)
{
	struct unlock_params *p = arg;

	file_unlock_inode_now(p->run_id, p->old_run_id, p->new_run_id);

	Free(p);
}

/*
 * file_alloc_unlock_params -- (internal) allocates structure for
 * file_unlock_inode, used as tx callback
 */
static struct unlock_params *
file_alloc_unlock_params(volatile uint64_t *run_id, uint64_t old_run_id,
		uint64_t new_run_id)
{
	struct unlock_params *p = Malloc(sizeof(*p));

	if (!p)
		return NULL;

	p->run_id = run_id;
	p->old_run_id = old_run_id;
	p->new_run_id = new_run_id;

	return p;
}

/*
 * file_unref_work -- (internal) decreases inode runtime reference counter,
 * possibly freeing the inode
 *
 * Must be called in transaction.
 * Inode must be already locked.
 */
static void
file_unref_work(PMEMfilepool *pfp, struct pmemfile_inode_rt *rt,
		TOID(struct pmemfile_inode) tinode)
{
	struct pmemfile_inode *inode = D_RW(tinode);
	volatile uint64_t *run_id = &inode->rt.run_id;
	struct unlock_params *unlock_arg = file_alloc_unlock_params(run_id,
			pfp->run_id - 1, pfp->run_id);
	if (!unlock_arg)
		pmemobj_tx_abort(errno);

	VALGRIND_MUTEX_LOCK_PRE(run_id, 1 /* is try lock */);
	while (!__sync_bool_compare_and_swap(run_id,
			pfp->run_id, pfp->run_id - 1))
		sched_yield();
	VALGRIND_MUTEX_LOCK_POST(run_id);

	if (__sync_sub_and_fetch(&rt->common.ref, 1) > 0) {
		file_unlock_inode(NULL, unlock_arg);

		pmemfile_inode_tx_unlock_on_commit(rt);
		return;
	}

	LOG(LTRC, "free inode 0x%lx path %s", tinode.oid.off,
			pmfi_path(tinode));

	TX_SET_DIRECT(inode, rt.data, NULL);

	struct pmemfile_inode_array *cur = rt->opened.arr;
	if (cur)
		pmemfile_inode_array_unregister(pfp, cur, rt->opened.idx);

	pmemfile_inode_tx_unlock_on_commit(rt);

	pmemobj_tx_stage_callback_push_back(TX_STAGE_ONCOMMIT,
			(pmemobj_tx_callback_basic)pmemfile_inode_rt_destroy,
			rt);
	nlink_t nlink = inode->nlink;

	unlock_arg->new_run_id = pfp->run_id - 2;
	if (nlink == 0) {
		file_unlock_inode(NULL, unlock_arg);

		pmemfile_inode_free(pfp, tinode);
	} else {
		pmemobj_tx_stage_callback_push_back(TX_STAGE_ONCOMMIT,
				file_unlock_inode,
				unlock_arg);
		pmemobj_tx_stage_callback_push_front(TX_STAGE_ONABORT,
				file_unlock_inode,
				unlock_arg);
	}
}

/*
 * pmemfile_inode_unref_locked -- decreases inode runtime reference counter
 *
 * Must be called in transaction.
 * Inode must be already locked.
 */
void
pmemfile_inode_unref_locked(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) tinode)
{
	LOG(LDBG, "inode 0x%lx path %s", tinode.oid.off, pmfi_path(tinode));

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	struct pmemfile_inode_rt *rt = pmemfile_inode_get(pfp, tinode);
	ASSERTne(rt, NULL);
	ASSERTne(rt->common.ref, 0);

	file_unref_work(pfp, rt, tinode);
}

/*
 * pmemfile_inode_unref -- decreases inode runtime reference counter
 *
 * Must be called in transaction.
 * Inode must NOT be locked.
 */
void
pmemfile_inode_unref(PMEMfilepool *pfp, TOID(struct pmemfile_inode) tinode)
{
	LOG(LDBG, "inode 0x%lx path %s", tinode.oid.off, pmfi_path(tinode));

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	struct pmemfile_inode_rt *rt = pmemfile_inode_get(pfp, tinode);
	ASSERTne(rt, NULL);
	ASSERTne(rt->common.ref, 0);

	pmemfile_inode_tx_wlock(rt);

	file_unref_work(pfp, rt, tinode);
}

/*
 * pmemfile_inode_unref_tx -- decreases inode runtime reference counter
 *
 * Must NOT be called in transaction.
 * Inode must NOT be locked.
 */
void
pmemfile_inode_unref_tx(PMEMfilepool *pfp, TOID(struct pmemfile_inode) tinode)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);

	TX_BEGIN(pfp->pop) {
		TX_SET_BASIC_CALLBACK(pfp->pop);
		pmemfile_inode_unref(pfp, tinode);
	} TX_ONABORT {
		FATAL("!");
	} TX_END
}

/*
 * pmemfile_inode_alloc -- allocates inode
 *
 * Must be called in transaction.
 */
TOID(struct pmemfile_inode)
pmemfile_inode_alloc(PMEMfilepool *pfp, uint64_t flags, struct timespec *t)
{
	LOG(LDBG, "flags 0x%lx", flags);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	TOID(struct pmemfile_inode) inode = TX_ZNEW(struct pmemfile_inode);

	if (clock_gettime(CLOCK_REALTIME, t)) {
		ERR("!clock_gettime");
		pmemobj_tx_abort(errno);
	}

	D_RW(inode)->flags = flags;
	D_RW(inode)->ctime = *t;
	D_RW(inode)->mtime = *t;
	D_RW(inode)->atime = *t;
	D_RW(inode)->nlink = 0;
	void *rt = pmemfile_inode_ref(pfp, inode);
	pmemobj_tx_stage_callback_push_front(TX_STAGE_ONABORT,
			(pmemobj_tx_callback_basic)pmemfile_inode_rt_destroy,
			rt);

	return inode;
}

/*
 * pmemfile_inode_free -- frees inode
 *
 * Must be called in transaction.
 */
void
pmemfile_inode_free(PMEMfilepool *pfp, TOID(struct pmemfile_inode) tinode)
{
	LOG(LDBG, "inode 0x%lx path %s", tinode.oid.off, pmfi_path(tinode));

	struct pmemfile_inode *inode = D_RW(tinode);
	if (_pmemfile_is_dir(inode)) {
		TOID(struct pmemfile_dir) dir = inode->file_data.dir;

		while (!TOID_IS_NULL(dir)) {
			/* should have been catched earlier */
			if (D_RW(dir)->used != 0)
				FATAL("Trying to free non-empty directory");

			TOID(struct pmemfile_dir) next = D_RW(dir)->next;
			TX_FREE(dir);
			dir = next;
		}
	} else if (_pmemfile_is_regular_file(inode)) {
		TOID(struct pmemfile_block_array) block_arr =
				inode->file_data.blocks;
		while (!TOID_IS_NULL(block_arr)) {
			struct pmemfile_block_array *arr = D_RW(block_arr);

			for (unsigned i = 0; i < arr->blocks_allocated; ++i)
				TX_FREE(arr->blocks[i].data);

			TOID(struct pmemfile_block_array) next = arr->next;
			TX_FREE(block_arr);
			block_arr = next;
		}

		if (!TOID_IS_NULL(inode->file_data.blocks))
			TX_SET_DIRECT(inode, file_data.blocks,
					TOID_NULL(struct pmemfile_block_array));
	} else {
		FATAL("unknown inode type 0x%lx", inode->flags);
	}
	TX_FREE(tinode);
}
