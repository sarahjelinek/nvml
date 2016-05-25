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
#ifndef	PMEMFILE_RUNTIME_H
#define	PMEMFILE_RUNTIME_H

/*
 * Runtime state structures.
 */

#include <pthread.h>
#include "layout.h"

struct ctree;

/* Pool */
struct pmemfilepool {
	PMEMobjpool *pop;
	uint64_t run_id;
	TOID(struct pmemfile_super) super;
};

/* File */
struct pmemfile {
	/* File inode. */
	TOID(struct pmemfile_inode) inode;

	/* Inode of a directory that this inode was opened from. */
	TOID(struct pmemfile_inode) parent;

	/*
	 * Protects against changes to offset / position cache from multiple
	 * threads.
	 */
	union {
		pthread_spinlock_t spin;
		pthread_mutex_t mutex;
	} lock;

	/* Requested/current position. */
	size_t offset;

	/* Current position cache. */
	struct pmemfile_pos {
		/* Current block array. */
		struct pmemfile_block_array *block_array;

		/* Block number in current block array. */
		unsigned block_id;

		/* Offset from the beginning of current block. */
		size_t block_offset;

		/* Above maps to below. */

		/* Offset from the beginning of file. */
		size_t global_offset;
	} pos;

	struct ctree *blocks;

	/* Open for read. */
	bool read;

	/* Open for write. */
	bool write;
};

struct pmemfile_urwlock {
	volatile uint64_t data;
};

struct pmemfile_common_rt {
	uint32_t ref;
};

/* Superblock */
struct pmemfile_super_rt {
	struct pmemfile_common_rt common;
	union {
		pthread_spinlock_t spin;
		pthread_rwlock_t rwlock;
		struct pmemfile_urwlock urwlock;
	} lock;
	TOID(struct pmemfile_super) super;
};

/* Directory */
struct pmemfile_dir_rt {
	struct pmemfile_common_rt common;
	TOID(struct pmemfile_dir) dir;
};

/* Inode */
struct pmemfile_inode_rt {
	struct pmemfile_common_rt common;
	union {
		pthread_spinlock_t spin;
		pthread_rwlock_t rwlock;
		struct pmemfile_urwlock urwlock;
	} lock;
	TOID(struct pmemfile_inode) inode;

#ifdef	DEBUG
	/*
	 * One of the full paths inode can be reached from.
	 * Used only for debugging.
	 */
	char *path;
#endif

	/* Pointer to the array of opened inodes. */
	struct {
		struct pmemfile_inode_array *arr;
		unsigned idx;
	} opened;
};

#endif
