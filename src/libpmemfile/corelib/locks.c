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

#include "internal.h"

/* struct pmemfile locking */
void
pmemfile_file_lock_init(struct pmemfile *file)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
		case 3:
			util_spin_init(&file->lock.spin, 0);
			break;
		case 4:
		case 5:
			util_mutex_init(&file->lock.mutex, NULL);
			break;
	}
}

void
pmemfile_file_lock(struct pmemfile *file)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
			util_spin_lock(&file->lock.spin);
			break;
		case 2:
		case 3:
			while (util_spin_trylock(&file->lock.spin))
				sched_yield();
			break;
		case 4:
		case 5:
			util_mutex_lock(&file->lock.mutex);
			break;
	}
}

void
pmemfile_file_unlock(struct pmemfile *file)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
		case 3:
			util_spin_unlock(&file->lock.spin);
			break;
		case 4:
		case 5:
			util_mutex_unlock(&file->lock.mutex);
			break;
	}
}

void
pmemfile_file_lock_destroy(struct pmemfile *file)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
		case 3:
			util_spin_destroy(&file->lock.spin);
			break;
		case 4:
		case 5:
			util_mutex_destroy(&file->lock.mutex);
			break;
	}
}

/* struct pmemfile_inode_rt locking */
void
pmemfile_inode_lock_init(struct pmemfile_inode_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
			util_spin_init(&rt->lock.spin, 0);
			break;
		case 3:
		case 4:
			pmemfile_urwlock_init(&rt->lock.urwlock);
			break;
		case 5:
			util_rwlock_init(&rt->lock.rwlock);
			break;
	}
}

void
pmemfile_inode_rlock(struct pmemfile_inode_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
			util_spin_lock(&rt->lock.spin);
			break;
		case 2:
			while (util_spin_trylock(&rt->lock.spin))
				sched_yield();
			break;
		case 3:
		case 4:
			pmemfile_urwlock_rlock(&rt->lock.urwlock);
			break;
		case 5:
			util_rwlock_rdlock(&rt->lock.rwlock);
			break;
	}
}

void
pmemfile_inode_wlock(struct pmemfile_inode_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
			util_spin_lock(&rt->lock.spin);
			break;
		case 2:
			while (util_spin_trylock(&rt->lock.spin))
				sched_yield();
			break;
		case 3:
		case 4:
			pmemfile_urwlock_wlock(&rt->lock.urwlock);
			break;
		case 5:
			util_rwlock_wrlock(&rt->lock.rwlock);
			break;
	}
}

void
pmemfile_inode_tx_wlock(struct pmemfile_inode_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
			pmemfile_tx_spin_lock(&rt->lock.spin);
			break;
		case 2:
			while (pmemfile_tx_spin_trylock(&rt->lock.spin))
				sched_yield();
			break;
		case 3:
		case 4:
			pmemfile_tx_urwlock_wlock(&rt->lock.urwlock);
			break;
		case 5:
			pmemfile_tx_rwlock_wlock(&rt->lock.rwlock);
			break;
	}
}

void
pmemfile_inode_tx_unlock_on_commit(struct pmemfile_inode_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
			pmemfile_tx_spin_unlock_on_commit(&rt->lock.spin);
			break;
		case 3:
		case 4:
			pmemfile_tx_urwlock_unlock_on_commit(&rt->lock.urwlock);
			break;
		case 5:
			pmemfile_tx_rwlock_unlock_on_commit(&rt->lock.rwlock);
			break;
	}
}

void
pmemfile_inode_unlock(struct pmemfile_inode_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
			util_spin_unlock(&rt->lock.spin);
			break;
		case 3:
		case 4:
			pmemfile_urwlock_unlock(&rt->lock.urwlock);
			break;
		case 5:
			util_rwlock_unlock(&rt->lock.rwlock);
			break;
	}
}

void
pmemfile_inode_lock_destroy(struct pmemfile_inode_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
			util_spin_destroy(&rt->lock.spin);
			break;
		case 3:
		case 4:
			pmemfile_urwlock_destroy(&rt->lock.urwlock);
			break;
		case 5:
			util_rwlock_destroy(&rt->lock.rwlock);
			break;
	}
}

/* struct pmemfile_super_rt locking */
void
pmemfile_super_lock_init(struct pmemfile_super_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
			util_spin_init(&rt->lock.spin, 0);
			break;
		case 3:
		case 4:
			pmemfile_urwlock_init(&rt->lock.urwlock);
			break;
		case 5:
			util_rwlock_init(&rt->lock.rwlock);
			break;
	}
}

void
pmemfile_super_tx_wlock(struct pmemfile_super_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
			pmemfile_tx_spin_lock(&rt->lock.spin);
			break;
		case 2:
			while (pmemfile_tx_spin_trylock(&rt->lock.spin))
				sched_yield();
			break;
		case 3:
		case 4:
			pmemfile_tx_urwlock_wlock(&rt->lock.urwlock);
			break;
		case 5:
			pmemfile_tx_rwlock_wlock(&rt->lock.rwlock);
			break;
	}
}

void
pmemfile_super_tx_unlock_on_commit(struct pmemfile_super_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
			pmemfile_tx_spin_unlock_on_commit(&rt->lock.spin);
			break;
		case 3:
		case 4:
			pmemfile_tx_urwlock_unlock_on_commit(&rt->lock.urwlock);
			break;
		case 5:
			pmemfile_tx_rwlock_unlock_on_commit(&rt->lock.rwlock);
			break;
	}
}

void
pmemfile_super_lock_destroy(struct pmemfile_super_rt *rt)
{
	switch (pmemfile_contention_level) {
		case 0:
			break;
		case 1:
		case 2:
			util_spin_destroy(&rt->lock.spin);
			break;
		case 3:
		case 4:
			pmemfile_urwlock_destroy(&rt->lock.urwlock);
			break;
		case 5:
			util_rwlock_destroy(&rt->lock.rwlock);
			break;
	}
}
