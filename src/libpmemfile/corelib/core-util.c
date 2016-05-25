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
 * core-util.c -- utility functions
 */

#include <errno.h>
#include <stdlib.h>

#include "internal.h"
#include "util.h"
#include "valgrind_internal.h"
#include "libpmemobj/sync.h"

/*
 * file_vptr_get -- (internal) race-free allocator of runtime state
 */
static struct pmemfile_common_rt *
file_vptr_get(PMEMfilepool *pfp, struct pmemfile_vptr *vptr,
		struct pmemfile_common_rt *(*constr)(void *arg), void *arg,
		bool ref)
{
	LOG(LTRC, "pfp %p vptr %p", pfp, vptr);

	/*
	 * Run_id is "generation identifier" - it's stored in the pool and
	 * is increased by 2 on each pool open.
	 *
	 * If vptr.run_id != pool.run_id it means vptr.data is invalid.
	 *
	 * vptr.run_id == pool.run_id - 1 is a special value which means that
	 * vptr is being updated.
	 *
	 * So the algorithm looks like this:
	 *
	 * If vptr.run_id == pool.run_id, vptr.data is valid.
	 *
	 * If vptr.run_id == pool.run_id - 1, wait.
	 *
	 * If vptr.run_id != pool.run_id and vptr.run_id != pool.run_id - 1,
	 * try to atomically store pool.run_id - 1 in vptr.run_id. If it
	 * succeeded, we have exclusive access to vptr and we can update
	 * vptr.data, update reference counter and store pool.run_id in
	 * vptr.run_id.
	 * If atomic store didn't succeed (because other thread managed to do
	 * it first), just wait.
	 *
	 * This algorithm is based on implementation of libpmemobj locks.
	 */

	volatile uint64_t *run_id = &vptr->run_id;
	uint64_t pool_run_id = pfp->run_id;
	uint64_t tmp_run_id;

	VALGRIND_REMOVE_PMEM_MAPPING(vptr, sizeof(*vptr));

	VALGRIND_MUTEX_LOCK_PRE(run_id, 1);
	while ((tmp_run_id = *run_id) != pool_run_id || ref) {
		/*
		 * Is allocation in being performed by other thread?
		 * If yes, try again later.
		 */
		if (tmp_run_id == pool_run_id - 1) {
			sched_yield();
			continue;
		}

		/*
		 * Try lock. If we raced with other thread, try again later.
		 */
		if (!__sync_bool_compare_and_swap(run_id, tmp_run_id,
				pool_run_id - 1)) {
			sched_yield();
			continue;
		}
		VALGRIND_MUTEX_LOCK_POST(run_id);

		/*
		 * If we entered this loop because of difference in run_id,
		 * it means runtime data has not been allocated yet.
		 * Allocate it.
		 */
		if (tmp_run_id != pool_run_id) {
			vptr->data = constr(arg);
			/*
			 * If allocation failed, we will try again in the next
			 * iteration.
			 */
		}

		/*
		 * If user wanted to increase reference counter and runtime
		 * data has already been allocated, we can just increase
		 * the counter.
		 */
		if (ref && vptr->data) {
			__sync_fetch_and_add(&vptr->data->ref, 1);
			ref = false;
		}

		uint64_t new_run_id;

		/*
		 * If allocation succeeded, we can set current run_id,
		 * if it didn't we have to pretend nothing happened.
		 */
		if (vptr->data)
			new_run_id = pool_run_id;
		else
			new_run_id = pool_run_id - 2;

		/* Unlock. */
		VALGRIND_MUTEX_UNLOCK_PRE(run_id);
		if (__sync_bool_compare_and_swap(run_id, pool_run_id - 1,
				new_run_id) == 0)
			FATAL("error setting runid");
		VALGRIND_MUTEX_UNLOCK_POST(run_id);
	}

	return vptr->data;
}

/*
 * file_inode_rt_construct -- (internal) inode runtime state constructor
 */
static struct pmemfile_common_rt *
file_inode_rt_construct(void *arg)
{
	struct pmemfile_inode_rt *rt = Zalloc(sizeof(*rt));
	if (!rt)
		return NULL;

	rt->inode = *((TOID(struct pmemfile_inode) *)arg);
	LOG(LTRC, "inode 0x%lx", rt->inode.oid.off);

	pmemfile_inode_lock_init(rt);

	return &rt->common;
}

/*
 * pmemfile_inode_rt_destroy -- destroys runtime state of the inode
 */
void
pmemfile_inode_rt_destroy(void *ctx, struct pmemfile_inode_rt *rt)
{
#ifdef	DEBUG
	/* "path" field is defined only in DEBUG builds */
	Free(rt->path);
#endif
	pmemfile_inode_lock_destroy(rt);
	Free(rt);
}

/*
 * _pmemfile_inode_get -- type-safe inode runtime state allocation helper
 *
 * Use pmemfile_inode_get or pmemfile_inode_ref.
 */
struct pmemfile_inode_rt *
_pmemfile_inode_get(PMEMfilepool *pfp, TOID(struct pmemfile_inode) tinode,
		bool ref)
{
	LOG(LTRC, "inode 0x%lx", tinode.oid.off);
	struct pmemfile_inode *inode = D_RW(tinode);

	return (void *)file_vptr_get(pfp, &inode->rt, file_inode_rt_construct,
			&tinode, ref);
}

/*
 * file_super_rt_construct -- superblock runtime state constructor
 */
static struct pmemfile_common_rt *
file_super_rt_construct(void *arg)
{
	struct pmemfile_super_rt *rt = Zalloc(sizeof(*rt));
	if (!rt)
		return NULL;

	rt->super = *((TOID(struct pmemfile_super) *)arg);
	LOG(LTRC, "super 0x%lx", rt->super.oid.off);

	pmemfile_super_lock_init(rt);

	return &rt->common;
}

/*
 * pmemfile_super_rt_destroy -- superblock runtime state destructor
 */
void
pmemfile_super_rt_destroy(void *ctx, struct pmemfile_super_rt *rt)
{
	pmemfile_super_lock_destroy(rt);
	Free(rt);
}

/*
 * _pmemfile_super_get -- type-safe superblock runtime state allocation helper
 *
 * Use pmemfile_super_get.
 */
struct pmemfile_super_rt *
_pmemfile_super_get(PMEMfilepool *pfp, TOID(struct pmemfile_super) tsuper,
		bool ref)
{
	LOG(LTRC, "super 0x%lx", tsuper.oid.off);
	struct pmemfile_super *super = D_RW(tsuper);

	return (void *)file_vptr_get(pfp, &super->rt, file_super_rt_construct,
			&tsuper, ref);
}

/*
 * file_dir_rt_construct -- directory runtime state constructor
 */
static struct pmemfile_common_rt *
file_dir_rt_construct(void *arg)
{
	struct pmemfile_dir_rt *rt = Zalloc(sizeof(*rt));
	if (!rt)
		return NULL;

	rt->dir = *((TOID(struct pmemfile_dir) *)arg);
	LOG(LTRC, "dir 0x%lx", rt->dir.oid.off);

	return &rt->common;
}

/*
 * _pmemfile_dir_get -- type-safe directory runtime state allocation helper
 *
 * Use pmemfile_dir_get.
 */
struct pmemfile_dir_rt *
_pmemfile_dir_get(PMEMfilepool *pfp, TOID(struct pmemfile_dir) tdir, bool ref)
{
	LOG(LTRC, "dir 0x%lx", tdir.oid.off);
	struct pmemfile_dir *dir = D_RW(tdir);

	return (void *)file_vptr_get(pfp, &dir->rt, file_dir_rt_construct,
			&tdir, ref);
}

static void
file_util_rwlock_unlock(void *ctx, void *arg)
{
	util_rwlock_unlock(arg);
}

/*
 * pmemfile_tx_rwlock_wrlock -- transactional read-write lock
 */
void
pmemfile_tx_rwlock_wlock(pthread_rwlock_t *l)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	util_rwlock_wrlock(l);

	pmemobj_tx_stage_callback_push_front(TX_STAGE_ONABORT,
			file_util_rwlock_unlock, l);
}

/*
 * pmemfile_tx_rwlock_unlock_on_commit -- transactional read-write unlock
 */
void
pmemfile_tx_rwlock_unlock_on_commit(pthread_rwlock_t *l)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	pmemobj_tx_stage_callback_push_back(TX_STAGE_ONCOMMIT,
			file_util_rwlock_unlock, l);
}

static void
file_util_urwlock_unlock(void *ctx, void *arg)
{
	pmemfile_urwlock_unlock(arg);
}

/*
 * pmemfile_tx_urwlock_wlock -- transactional read-write lock
 */
void
pmemfile_tx_urwlock_wlock(struct pmemfile_urwlock *l)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	pmemfile_urwlock_wlock(l);

	pmemobj_tx_stage_callback_push_front(TX_STAGE_ONABORT,
			file_util_urwlock_unlock, l);
}

/*
 * pmemfile_tx_urwlock_unlock_on_commit -- transactional read-write unlock
 */
void
pmemfile_tx_urwlock_unlock_on_commit(struct pmemfile_urwlock *l)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	pmemobj_tx_stage_callback_push_back(TX_STAGE_ONCOMMIT,
			file_util_urwlock_unlock, l);
}

static void
file_util_spin_unlock(void *ctx, void *arg)
{
	util_spin_unlock(arg);
}

/*
 * pmemfile_spin_lock -- transactional spin lock
 */
void
pmemfile_tx_spin_lock(pthread_spinlock_t *l)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	util_spin_lock(l);

	pmemobj_tx_stage_callback_push_front(TX_STAGE_ONABORT,
			file_util_spin_unlock, (void *)l);
}

/*
 * pmemfile_spin_trylock -- transactional spin lock
 */
int
pmemfile_tx_spin_trylock(pthread_spinlock_t *l)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	int r = util_spin_trylock(l);
	if (r)
		return r;

	pmemobj_tx_stage_callback_push_front(TX_STAGE_ONABORT,
			file_util_spin_unlock, (void *)l);
	return r;
}

/*
 * pmemfile_spin_unlock_on_commit -- transactional spin unlock
 */
void
pmemfile_tx_spin_unlock_on_commit(pthread_spinlock_t *l)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	pmemobj_tx_stage_callback_push_back(TX_STAGE_ONCOMMIT,
			file_util_spin_unlock, (void *)l);
}

/*
 * pmemfile_pmemobj_mutex_unlock_on_abort -- postpones pmemobj_mutex_unlock on
 * transaction abort
 */
static void
pmemfile_tx_pmemobj_mutex_unlock_on_abort(PMEMmutex *mutexp)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	pmemobj_tx_stage_callback_push_front(TX_STAGE_ONABORT,
			(pmemobj_tx_callback_basic)pmemobj_mutex_unlock_nofail,
			mutexp);
}

/*
 * pmemfile_pmemobj_mutex_lock -- transactional pmemobj_mutex_lock
 */
static void
pmemfile_tx_pmemobj_mutex_lock(PMEMfilepool *pfp, PMEMmutex *mutexp)
{
	pmemobj_mutex_lock_nofail(pfp->pop, mutexp);

	pmemobj_tx_stage_callback_push_front(TX_STAGE_ONABORT,
			(pmemobj_tx_callback_basic)pmemobj_mutex_unlock_nofail,
			mutexp);
}

/*
 * pmemfile_pmemobj_mutex_unlock_on_commit -- postpones pmemobj_mutex_unlock on
 * transaction commit
 */
static void
pmemfile_tx_pmemobj_mutex_unlock_on_commit(PMEMmutex *mutexp)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	pmemobj_tx_stage_callback_push_back(TX_STAGE_ONCOMMIT,
			(pmemobj_tx_callback_basic)pmemobj_mutex_unlock_nofail,
			mutexp);
}

/*
 * pmemfile_inode_array_add_single -- finds space for 1 inode in specified
 * array, inserts it there and returns success status
 */
static bool
pmemfile_inode_array_add_single(PMEMfilepool *pfp,
		struct pmemfile_inode_array *cur,
		TOID(struct pmemfile_inode) inode,
		struct pmemfile_inode_array **ins,
		unsigned *ins_idx)
{
	for (unsigned i = 0; i < NUMINODES_PER_ENTRY; ++i) {
		if (!TOID_IS_NULL(cur->inodes[i]))
			continue;

		COMPILE_ERROR_ON(
			offsetof(struct pmemfile_inode_array, mtx) != 0);

		pmemfile_tx_pmemobj_mutex_unlock_on_abort(&cur->mtx);

		pmemobj_tx_add_range_direct(
			(char *)cur + sizeof(cur->mtx),
			sizeof(*cur) - sizeof(cur->mtx));
		cur->inodes[i] = inode;
		cur->used++;

		if (ins)
			*ins = cur;
		if (ins_idx)
			*ins_idx = i;

		return true;
	}

	return false;
}

/*
 * pmemfile_inode_array_add -- adds inode to array, returns its position
 *
 * Must be called in transaction.
 */
void
pmemfile_inode_array_add(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode_array) array,
		TOID(struct pmemfile_inode) inode,
		struct pmemfile_inode_array **ins,
		unsigned *ins_idx)
{
	bool found = false;
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	do {
		struct pmemfile_inode_array *cur = D_RW(array);

		pmemobj_mutex_lock_nofail(pfp->pop, &cur->mtx);

		if (cur->used < NUMINODES_PER_ENTRY)
			found = pmemfile_inode_array_add_single(pfp, cur, inode,
					ins, ins_idx);

		bool modified = false;
		if (!found) {
			if (TOID_IS_NULL(cur->next)) {
				pmemfile_tx_pmemobj_mutex_unlock_on_abort(
						&cur->mtx);

				TX_SET_DIRECT(cur, next,
					TX_ZNEW(struct pmemfile_inode_array));
				D_RW(cur->next)->prev = array;

				modified = true;
			}

			array = cur->next;
		}

		if (found || modified)
			pmemfile_tx_pmemobj_mutex_unlock_on_commit(&cur->mtx);
		else
			pmemobj_mutex_unlock_nofail(pfp->pop, &cur->mtx);

	} while (!found);
}

/*
 * pmemfile_inode_array_unregister -- removes inode from specified place in
 * array
 *
 * Must be called in transaction.
 */
void
pmemfile_inode_array_unregister(PMEMfilepool *pfp,
		struct pmemfile_inode_array *cur,
		unsigned idx)
{
	pmemfile_tx_pmemobj_mutex_lock(pfp, &cur->mtx);

	ASSERT(cur->used > 0);

	COMPILE_ERROR_ON(offsetof(struct pmemfile_inode_array, mtx) != 0);
	pmemobj_tx_add_range_direct(
		(char *)cur + sizeof(cur->mtx),
		sizeof(*cur) - sizeof(cur->mtx));
	cur->inodes[idx] = TOID_NULL(struct pmemfile_inode);
	cur->used--;

	pmemfile_tx_pmemobj_mutex_unlock_on_commit(&cur->mtx);

}
