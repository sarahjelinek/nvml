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
int null_fd;


/*
 * pmemfile_intercept_write -- this gets control when libc's write() is called
 *
 */
static ssize_t
pmemfile_intercept_write(int fd, const void *buf, size_t count)
{
	ssize_t retval = 0;

	PMEMfile *filep = (PMEMfile *)(uintptr_t)ctree_find(fd_list, (uint64_t)fd);
	if (filep != NULL)
		retval = intercept_write(pfp, filep, buf, count);
	else
		retval = -1;
	return retval;
}

/*
 * pmemfile_intercept_read -- this gets control when libc's read() is called
 */
static ssize_t
pmemfile_intercept_read(int fd, void *buf, size_t count)
{
	ssize_t retval = 0;

	PMEMfile *filep = (PMEMfile *)(uintptr_t)ctree_find(fd_list, (uint64_t)fd);
	LOG(LDBG, "filep %p", filep);

	if (filep != NULL)
		retval = intercept_read(pfp, filep, buf, count);
	else
		return -1;
	return retval;
}

/*
 * pmemfile_intercept_open -- this gets control when libc's open() is called
 */
static ssize_t
pmemfile_intercept_open(const char *pathname, int flags, mode_t mode)
{
	ssize_t retval = 0;

	if (!strncmp(pathname, PMEMFILE_FS, strlen(PMEMFILE_FS))) {
		/* XXX: Short term fix: strip out all but file name */
		const char *newpath = strrchr(pathname, '/');
		retval = intercept_open(pfp, fd_list, newpath, flags, mode);
	} else {
		return -1;
	}
	return retval;
	
}

static ssize_t
pmemfile_intercept_close(int fd)
{
	ssize_t retval = 0;


	PMEMfile *filep = (PMEMfile *)(uintptr_t)ctree_find(fd_list, (uint64_t)fd);
	LOG(LDBG, "fd %d, filep %p\n", fd, filep);
	if (filep != NULL)
		retval = intercept_close(pfp, filep);
	else
		retval = -1;
	return retval;
}
	
/*
 * pmemfile_patch -- hot patch the code at "from" to jump to "to"
 *
 * This is x86_64 specific.
 *
 * The code at "from" is overwritten to contain a indirect jump instruction to
 * the new routine at "to". The address is a 64 bit absolute address.
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
	unsigned char *f = (unsigned char *)from;

        unsigned long long pgbegin = (unsigned long long)from & PAGE_MASK;
        unsigned long long pgend = ((unsigned long long)from + 12) & PAGE_MASK;	

	/* allow writes to the normally read-only code pages */
        if (mprotect((void *) pgbegin, pgend - pgbegin + PAGE_SIZE,
		PROT_READ|PROT_WRITE|PROT_EXEC) < 0)
			err(1, "elmo mprotect");	

	f[0] = 0x49;
	f[1] = 0xbb;
	memcpy(&f[2], &to, 8);
	f[10] = 0x41;
	f[11] = 0xff;
	f[12] = 0xe3;
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
	if (!fd_list)
		return;

	if (getenv("PMEMFILE_FS_SETUP")) {
		/*
		 * If we have not opened or created the pool and this is
		 * a pmem resident file, do so now.
		 */
		if (pfp == NULL) {
			if (access(PMEMFILE_FS, F_OK) != 0) {
				pfp = pmemfile_mkfs(PMEMFILE_FS, PMEMOBJ_MIN_POOL, 
							S_IRWXU | S_IRWXG| S_IRWXO);
			} else {
				pfp = pmemfile_pool_open(PMEMFILE_FS);
			}
		}
	}

	null_fd = open("/dev/null", O_RDONLY);
	if (null_fd < 0) {
		fprintf(stderr, "Could not open /dev/null\n");
		exit(1);
	}

	//pmemfile_patch((void *) write, (void *) pmemfile_intercept_write);

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
