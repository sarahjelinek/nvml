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
#ifndef	PMEMFILE_LOCKS_H
#define	PMEMFILE_LOCKS_H

#include "runtime.h"

/* struct pmemfile locking */
void pmemfile_file_lock_init(struct pmemfile *file);
void pmemfile_file_lock(struct pmemfile *file);
void pmemfile_file_unlock(struct pmemfile *file);
void pmemfile_file_lock_destroy(struct pmemfile *file);

/* struct pmemfile_inode_rt locking */
void pmemfile_inode_lock_init(struct pmemfile_inode_rt *rt);
void pmemfile_inode_rlock(struct pmemfile_inode_rt *rt);
void pmemfile_inode_wlock(struct pmemfile_inode_rt *rt);
void pmemfile_inode_tx_wlock(struct pmemfile_inode_rt *rt);
void pmemfile_inode_tx_unlock_on_commit(struct pmemfile_inode_rt *rt);
void pmemfile_inode_unlock(struct pmemfile_inode_rt *rt);
void pmemfile_inode_lock_destroy(struct pmemfile_inode_rt *rt);

/* struct pmemfile_super_rt locking */
void pmemfile_super_lock_init(struct pmemfile_super_rt *rt);
void pmemfile_super_tx_wlock(struct pmemfile_super_rt *rt);
void pmemfile_super_tx_unlock_on_commit(struct pmemfile_super_rt *rt);
void pmemfile_super_lock_destroy(struct pmemfile_super_rt *rt);

#endif
