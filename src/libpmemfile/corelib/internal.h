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
#ifndef	PMEMFILE_INTERNAL_H
#define	PMEMFILE_INTERNAL_H

/*
 * Internal API.
 */

#include "libpmemfile-core.h"
#include "libpmemobj.h"
#include "locks.h"
#include "runtime.h"
#include "out.h"
#include "sys_util.h"

#define	LSUP 1  /* unsupported feature */
#define	LUSR 2  /* user error */
#define	LINF 3  /* information */
#define	LDBG 4  /* debug info */
#define	LTRC 10 /* traces, very verbose */

extern size_t pmemfile_core_block_size;
extern int pmemfile_optimized_list_walk;
extern int pmemfile_optimized_tree_walk;
extern int pmemfile_contention_level;

TOID(struct pmemfile_inode) pmemfile_inode_alloc(PMEMfilepool *pfp,
		uint64_t flags, struct timespec *t);

struct pmemfile_inode_rt *pmemfile_inode_ref(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) inode);
struct pmemfile_inode_rt *pmemfile_inode_ref_set_path(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) inode,
		TOID(struct pmemfile_inode) parent_inode,
		const char *name);

void pmemfile_inode_unref_locked(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) tinode);
void pmemfile_inode_unref(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) tinode);
void pmemfile_inode_unref_tx(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) tinode);

const char *pmfi_path(TOID(struct pmemfile_inode) inode);

void pmemfile_inode_free(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) tinode);

static inline bool _pmemfile_is_dir(const struct pmemfile_inode *inode)
{
	return S_ISDIR(inode->flags);
}

static inline bool pmemfile_is_dir(TOID(struct pmemfile_inode) inode)
{
	return _pmemfile_is_dir(D_RO(inode));
}

static inline bool _pmemfile_is_regular_file(const struct pmemfile_inode *inode)
{
	return S_ISREG(inode->flags);
}

static inline bool pmemfile_is_regular_file(TOID(struct pmemfile_inode) inode)
{
	return _pmemfile_is_regular_file(D_RO(inode));
}

TOID(struct pmemfile_inode) pmemfile_new_dir(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) parent, const char *name);

void pmemfile_add_dentry(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) parent_inode,
		const char *name,
		TOID(struct pmemfile_inode) child_inode,
		const struct timespec *tm);

void pmemfile_inode_rt_destroy(void *ctx, struct pmemfile_inode_rt *rt);
void pmemfile_super_rt_destroy(void *ctx, struct pmemfile_super_rt *rt);

struct pmemfile_inode_rt *_pmemfile_inode_get(PMEMfilepool *pfp,
	TOID(struct pmemfile_inode) tinode, bool ref);
struct pmemfile_super_rt *_pmemfile_super_get(PMEMfilepool *pfp,
	TOID(struct pmemfile_super) tsuper, bool ref);
struct pmemfile_dir_rt *_pmemfile_dir_get(PMEMfilepool *pfp,
	TOID(struct pmemfile_dir) tdentry, bool ref);

/*
 * pmemfile_inode_get -- returns runtime state of inode
 */
static inline struct pmemfile_inode_rt *
pmemfile_inode_get(PMEMfilepool *pfp, TOID(struct pmemfile_inode) tinode)
{
	struct pmemfile_inode *inode = D_RW(tinode);
	if (pfp->run_id == inode->rt.run_id && inode->rt.data)
		return (void *)inode->rt.data;

	return _pmemfile_inode_get(pfp, tinode, false);
}

/*
 * pmemfile_super_get -- returns runtime state of super block
 */
static inline struct pmemfile_super_rt *
pmemfile_super_get(PMEMfilepool *pfp, TOID(struct pmemfile_super) tsuper)
{
	struct pmemfile_super *super = D_RW(tsuper);
	if (pfp->run_id == super->rt.run_id && super->rt.data)
		return (void *)super->rt.data;

	return _pmemfile_super_get(pfp, tsuper, false);
}

/*
 * pmemfile_dir_get -- returns runtime state of directory
 */
static inline struct pmemfile_dir_rt *
pmemfile_dir_get(PMEMfilepool *pfp, TOID(struct pmemfile_dir) tdir)
{
	struct pmemfile_dir *dir = D_RW(tdir);
	if (pfp->run_id == dir->rt.run_id && dir->rt.data)
		return (void *)dir->rt.data;

	return _pmemfile_dir_get(pfp, tdir, false);
}

void pmemfile_set_path_debug(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) parent_inode,
		TOID(struct pmemfile_inode) child_inode,
		const char *name);

struct pmemfile_dentry *pmemfile_lookup_dentry_locked(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) parent,
		const char *name,
		struct pmemfile_dir **outdir);
TOID(struct pmemfile_inode) pmemfile_lookup_dentry(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) parent, const char *name);

void pmemfile_unlink_dentry(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) parent,
		const char *name,
		volatile struct pmemfile_inode_rt **rt);
void _pmemfile_list(PMEMfilepool *pfp, TOID(struct pmemfile_inode) parent);

void pmemfile_tx_rwlock_wlock(pthread_rwlock_t *l);
void pmemfile_tx_rwlock_unlock_on_commit(pthread_rwlock_t *l);

void pmemfile_tx_spin_lock(pthread_spinlock_t *l);
int pmemfile_tx_spin_trylock(pthread_spinlock_t *l);
void pmemfile_tx_spin_unlock_on_commit(pthread_spinlock_t *l);

void pmemfile_tx_urwlock_wlock(struct pmemfile_urwlock *l);
void pmemfile_tx_urwlock_unlock_on_commit(struct pmemfile_urwlock *l);

void pmemfile_inode_array_add(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode_array) array,
		TOID(struct pmemfile_inode) inode,
		struct pmemfile_inode_array **ins,
		unsigned *ins_idx);
void pmemfile_inode_array_unregister(PMEMfilepool *pfp,
		struct pmemfile_inode_array *cur,
		unsigned idx);

void pmemfile_destroy_data_state(PMEMfile *file);

static inline void pmemfile_urwlock_init(struct pmemfile_urwlock *lock)
{
	lock->data = 0;
}

static inline void pmemfile_urwlock_rlock(struct pmemfile_urwlock *lock)
{
	uint64_t oldval, newval;
	do {
		oldval = lock->data & 0xffffffff;
		newval = oldval + 1;
	} while (!__sync_bool_compare_and_swap(&lock->data, oldval, newval));
}

static inline void pmemfile_urwlock_wlock(struct pmemfile_urwlock *lock)
{
	while (!__sync_bool_compare_and_swap(&lock->data, 0, 1UL << 32))
		;
}

static inline void pmemfile_urwlock_unlock(struct pmemfile_urwlock *lock)
{
	if (lock->data & (1UL << 32)) {
		if (!__sync_bool_compare_and_swap(&lock->data, 1UL << 32, 0))
			FATAL("impossible");
	} else {
		uint64_t oldval, newval;
		do {
			oldval = lock->data & 0xffffffff;
			newval = oldval - 1;
		} while (!__sync_bool_compare_and_swap(&lock->data, oldval,
				newval));
	}
}

static inline void pmemfile_urwlock_destroy(struct pmemfile_urwlock *lock)
{
}

#endif
