/*
 * pmemfile-intercept.c
 *
 * Force load this using LD_PRELOAD.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <err.h>
#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/reg.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>

#include "intercept_run.h"
#include "libpmemfile-core.h"
#include "libpmemobj.h"
#include "runtime.h"
#include "out.h"
#include "sys_util.h"
#include "util.h"

#define PMEMFILE_FS "/mnt/pmem/pmemfs"
//#define UNUSED(x) (void)(x)

#define PMEMFILE_LOG_PREFIX "libpmemfile-intercept"
#define PMEMFILE_LOG_LEVEL_VAR "PMEMFILEINTERCEPT_LOG_LEVEL"
#define PMEMFILE_LOG_FILE_VAR "PMEMFILEINTERCEPT_LOG_FILE"

#define LDBG	4


/*
 * pmemfile_intercept_write -- this gets control when libc's write() is called
 *
 */
static ssize_t
pmemfile_intercept_write(int fd, const void *buf, size_t count)
{
	ssize_t retval = 0;
	retval = syscall(SYS_write, fd, buf, count);
	return retval;
}

/*
 * pmemfile_intercept_read -- this gets control when libc's read() is called
 */
static ssize_t
pmemfile_intercept_read(int fd, void *buf, size_t count)
{
	ssize_t retval = 0;

	retval = syscall(SYS_read, fd, buf, count);
	return retval;
}

/*
 * pmemfile_intercept_open -- this gets control when libc's open() is called
 */
static int
pmemfile_intercept_open(const char *pathname, int flags, mode_t mode)
{
	//LOG(LDBG, "pathname %s flags 0x%x mode %o", pathname, flags, mode);
	fprintf(stderr, "pmemfile_intercept_open pathname %s, flags %d, mode %o\n",
		pathname, flags, mode);
	PMEMfile *filep;
	PMEMfilepool *pfp = pmemfile_pool_open(PMEMFILE_FS);
	
	if (pfp != NULL) {
		//LOG(LDBG, "pmemfile_intercept_open pool %p", pfp);
		filep = pmemfile_open(pfp, pathname, flags, mode);
		fprintf(stderr, "filep %p\n", filep);
	} else {
		//LOG(LDBG, "pmemfile_pool_open failed pathname %s", pathname);
		return 0;
	}
	//fprintf(stderr, "filep %p\n", filep);
	//return filep->fileno;

	//fprintf(stderr, "pmemfile_intercept_open pathname %s, flags %d, mode %o\n",
			//pathname, flags, mode);
	//ssize_t retval = syscall(SYS_open, pathname, flags, mode);
        //return retval;	
	return 30;
}

/*
 * pmemfile_patch -- hot patch the code at "from" to jump to "to"
 *
 * This is x86_64 specific.
 *
 * The code at "from" is overwritten to contain a direct jump instruction to
 * the new routine at "to". A direct jump is 5 bytes long.
 * TODO: 
 * 	save off instructions overwritten to jump back if required.
 *		The saving of instructions requires that we disassmble
 * 		the function to ensure that we save off the correct
 * 		instructions. It is possible that we have to save off more
 *		than 5 bytes if an instruction in the 5 byte range is longer
 *		than 1 byte. The 5 bytes could fall into the middle of
 *		an instruction.
 *	Only disassemble the patched function one time for performance.
 * 	Only call mprotect one time on the same page.
 *
 */
static void
pmemfile_patch(void *from, void *to)
{
	fprintf(stderr, "pmemfile_patch from %p, to %p\n", from, to);
	unsigned long long pgbegin = (unsigned long long)from & PAGE_MASK;
        unsigned long long pgend = ((unsigned long long)from + 5) & PAGE_MASK;

        /* allow writes to the normally read-only code pages */
        if (mprotect((void *) pgbegin, pgend - pgbegin + PAGE_SIZE,
                                PROT_READ|PROT_WRITE|PROT_EXEC) < 0)
                err(1, "intercept mprotect");

	// unconditional jump opcode
	*(unsigned long *)from = 0xe9;

	// store the relative address from this opcode to our hook function
	*(unsigned long *)(from + 1) = 
		(unsigned char *)to - (unsigned char *)from - 5;	
}
/*
 * pmemflle_intercept_constructor -- constructor for intercept library
 *
 * Called automatically by the run-time loader.
 */
__attribute__((constructor))
static void
libpmemfile_inercept_init(void)
{
	out_init(PMEMFILE_LOG_PREFIX, PMEMFILE_LOG_LEVEL_VAR, PMEMFILE_LOG_FILE_VAR,
		PMEMFILE_MAJOR_VERSION, PMEMFILE_MINOR_VERSION);
	LOG(LDBG, NULL);

	/* Create the pool if it doesn't exis. TODO: This is temporary */
	if (access(PMEMFILE_FS, F_OK) != 0)
		(void)pmemfile_mkfs(PMEMFILE_FS, PMEMOBJ_MIN_POOL, 
				    S_IWUSR | S_IRUSR);
	pmemfile_patch((void *) open, (void *) pmemfile_intercept_open);
	pmemfile_patch((void *) write, (void *) pmemfile_intercept_write);
	pmemfile_patch((void *) read, (void *) pmemfile_intercept_read);
}

/*
 * libpmemfile_intercept_fini -- libpmemfile-core cleanup routine
 *
 * Called automatically when the process terminates.
 */
__attribute__((destructor))
static void
libpmemfile_core_fini(void)
{
        LOG(LDBG, NULL);
        out_fini();
}
