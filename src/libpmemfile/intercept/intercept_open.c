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

#include "intercept.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
intercept_open(PMEMfilepool *pfp, struct ctree *fd_list, const char *pathname,
		int flags, mode_t mode)
{

	PMEMfile *filep;
	int fd = 0;
	int ret = 0;

	extern int null_fd;

	filep = pmemfile_open(pfp, pathname, flags, mode);
	if (filep == NULL) {
		return -1;
	}
	/* Talk to Marcin about a better inerface to get fd */
	if (flags & O_CREAT) {
		fd = dup(null_fd);
		if (fd < 0) {
			pmemfile_close(pfp, filep);
			return fd;
		} else {
			ret = ctree_insert(fd_list, (uint64_t)fd, 
					(uintptr_t)filep);
			if (ret < 0) {
				pmemfile_close(pfp, filep);
				return -1;
			}
		}
	} else {
		fd = dup(null_fd);
		if (fd < 0) {
			pmemfile_close(pfp, filep);
			return fd;
		}
		ret = ctree_insert(fd_list, (uint64_t)fd, (uintptr_t)filep);
		if (ret < 0) {
			pmemfile_close(pfp, filep);
			return -1;
		}
	}
		
	return fd;
}
