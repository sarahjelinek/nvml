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

#include "intercept.h"

#define PMEMFILE_FS "pmemfile_pool/pool1"
//#define UNUSED(x) (void)(x)

#define PMEMFILE_INT_LOG_PREFIX "libpmemfile-intercept"
#define PMEMFILE_INT_LOG_LEVEL_VAR "PMEMFILEINTERCEPT_LOG_LEVEL"
#define PMEMFILE_INT_LOG_FILE_VAR "PMEMFILEINTERCEPT_LOG_FILE"

static PMEMfilepool *pfp;
static struct ctree *fd_list;


/*
 * pmemfile_intercept_write -- this gets control when libc's write() is called
 *
 */
static ssize_t
pmemfile_intercept_write(int fd, const void *buf, size_t count)
{
	ssize_t retval = 0;

	/*
	 * Check to see if this fd is a pmem resident file.
	 */
	PMEMfile *filep = (PMEMfile *)(uintptr_t)ctree_find(fd_list, (uint64_t)fd);
	LOG(LDBG, "filep %p", filep);
	
	if (filep != NULL)
		retval = intercept_write(pfp, filep, buf, count);
	else
		retval = syscall(SYS_write, fd, buf, count);

	return retval;
}

/*
 * pmemfile_intercept_read -- this gets control when libc's read() is called
 */
static ssize_t
pmemfile_intercept_read(int fd, void *buf, size_t count)
{
	ssize_t retval = -1;

	PMEMfile *filep = (PMEMfile *)(uintptr_t)ctree_find(fd_list, (uint64_t)fd);
	LOG(LDBG, "filep %p\n", filep);

	if (filep != NULL)
		retval = intercept_read(pfp, filep, buf, count);
	else
		retval = syscall(SYS_read, fd, buf, count);
	return retval;
}

/*
 * pmemfile_intercept_open -- this gets control when libc's open() is called
 */
static ssize_t
pmemfile_intercept_open(const char *pathname, int flags, mode_t mode)
{
	ssize_t retval = -1;

	if (pfp) {
		retval = intercept_open(pfp, fd_list, pathname, flags, mode);
	} else {
		retval = syscall(SYS_open, pathname, flags);
	}
	return retval;
	
}

static ssize_t
pmemfile_intercept_close(int fd)
{
	ssize_t ret = 0;

	PMEMfile *filep = (PMEMfile *)(uintptr_t)ctree_find(fd_list, (uint64_t)fd);
	if (filep != NULL)
		ret = intercept_close(pfp, filep);
	else
		ret = syscall(SYS_close, fd);
	return ret;
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
 * 		instructions.  t is possible that we have to save off more
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

	LOG(LDBG, "pmemfile_patch");
	fprintf(stderr, "pemmfile_patch from, to %p, %p\n", from, to);
	unsigned long long pgbegin = (unsigned long long)from & PAGE_MASK;
        unsigned long long pgend = ((unsigned long long)from + 5) & PAGE_MASK;

        /* allow writes to the normally read-only code pages */
        if (mprotect((void *) pgbegin, pgend - pgbegin + PAGE_SIZE,
                                PROT_READ|PROT_WRITE|PROT_EXEC) < 0) {
		fprintf(stderr, "mprotect failed for page %p starting for length,\
			%llu\n",  (void *)pgbegin, pgend-pgbegin + PAGE_SIZE);
                err(1, "intercept mprotect");
	}

	fprintf(stderr, "mprotect succeeded for page %p starting for length,\
		%llu\n", (void *)pgbegin, pgend-pgbegin + PAGE_SIZE);
	char *f = from;
	char *t = to;
	fprintf(stderr, "from, to %p, %p\n", f, t);

	// unconditional jump opcode
	*f = (char)0xe9; 

	// store the relative address from this opcode to our hook function
	*(long *)(f + 1) = t - f - 5;	
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
	out_init(PMEMFILE_INT_LOG_PREFIX, PMEMFILE_INT_LOG_LEVEL_VAR, 
			PMEMFILE_INT_LOG_FILE_VAR, PMEMFILE_INT_MAJOR_VERSION, 
			PMEMFILE_INT_MINOR_VERSION);
	LOG(LDBG, NULL);

	fd_list = ctree_new();

	if (getenv("PMEMFILE_FS_SETUP")) {
		/*
		 * If we have not opened or created the pool and this is
		 * a pmem resident file, do so now.
		 */
		if (pfp == NULL) {
			fprintf(stderr, "pfp is null\n");
			if (access(PMEMFILE_FS, F_OK) != 0) {
				fprintf(stderr, "calling pmemfile_mkfs\n");
				pfp = pmemfile_mkfs(PMEMFILE_FS, PMEMOBJ_MIN_POOL, 
							S_IWUSR | S_IRUSR);
			} else {
				fprintf(stderr, "opening pool \n");
				pfp = pmemfile_pool_open(PMEMFILE_FS);
			}
		} else {
			fprintf(stderr, "pfp isn't null %p\n", pfp);
		}
		
	}

	pmemfile_patch((void *) write, (void *) pmemfile_intercept_write);
	pmemfile_patch((void *) open,  (void *) pmemfile_intercept_open);
	pmemfile_patch((void *) close, (void *) pmemfile_intercept_close);
	pmemfile_patch((void *) read,  (void *) pmemfile_intercept_read);
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
