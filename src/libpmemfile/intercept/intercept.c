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
 * intercept.c 
 *
 * Force load this using LD_PRELOAD.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <err.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/reg.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include "intercept_run.h"

/*
 * intercept_write -- this gets control when libc's write() is called
 *
 * If you print something from this routine, you'll get infinite recursion.
 */
static ssize_t
intercept_write(int fd, const void *buf, size_t count)
{
  ssize_t retval;
	retval = syscall(SYS_write, fd, buf, count);
  return retval;
}

/*
 * intercept_read -- this gets control when libc's read() is called
 */
static ssize_t
intercept_read(int fd, void *buf, size_t count)
{
	ssize_t retval = 0;

	retval = syscall(SYS_read, fd, buf, count);
	return retval;
}

int
intercept_open(const char *pathname, int flags)
{
	ssize_t retval = 0;
	int fd = open("/dev/null", "r");
	/*
	 * Call into the open interception function.
	 */
	retval = int_open(fd, pathname, flags);

  retval = syscall(SYS_open, pathname, flags);
  return retval;
}

/*
 * patch -- hot patch the code at "from" to jump to "to"
 *
 * This is x86_64 specific.
 *
 * The code at "from" is overwritten to contain a jump instruction to
 * the new routine at "to".  The patched routine is destroyed -- you never
 * jump back to it. Instead, the new routine is expected to use syscall(2)
 * to perform the function of the old routine as necessary.
 *
 * The mprotect() call could be optimized not to change protections more
 * than once on the same page.
 */
static void
patch(void *from, void *to)
{

  fprintf(stderr, "In patching function\n");
	//unsigned char *p = (unsigned char *)from;
	unsigned long long pgbegin = (unsigned long long)from & PAGE_MASK;
	unsigned long long pgend = ((unsigned long long)from + 12) & PAGE_MASK;

	/* allow writes to the normally read-only code pages */
	if (mprotect((void *) pgbegin, pgend - pgbegin + PAGE_SIZE,
				PROT_READ|PROT_WRITE|PROT_EXEC) < 0)
		err(1, "intercept mprotect");

  // unconditional jump opcode
  *(unsigned long *)from = 0xe9;

  // store the relative address from this opcode to our hook function
  *(unsigned long *)(from + 1) = (unsigned char *)to - (unsigned char *)from - 5;
}
/*
 * intercept_constructor -- constructor for intercept library
 *
 * Called automatically by the run-time loader.
 */
__attribute__((constructor))
static void
intercept_constructor(void)
{
  patch((void *) open, (void *) intercept_open);
	patch((void *) write, (void *) intercept_write);
	patch((void *) read, (void *) intercept_read);
}
