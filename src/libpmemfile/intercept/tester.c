/*
 * trivial program for testing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	char *msg = "Hello World"; 
	char *msg1 = "Goodbye World"; 
	char buf[8192];
	int fd;


	fprintf(stderr, "Hello from main\n");
	fd = open("pmemfile_pool/pool1/aaa", O_CREAT, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed\n");
		exit(1);
	} else {
		fprintf(stderr, "open succeeded: fd %d\n", fd);
	}
	
  	//ssize_t w = write(fd, msg, strlen(msg));
	//fprintf(stderr, "write %lu bytes to %d\n", w, fd);
  	close(fd);
	fprintf(stderr, "closed %d\n", fd);
	fd = open("pmemfile_pool/pool1/aaa", O_CREAT, O_RDWR);
	fprintf(stderr, "fd for aaa %d\n", fd);
  	ssize_t r = read(fd, buf, sizeof(buf) - 1);
	fprintf(stderr, "read %lu bytes from %d\n", r, fd);
	close(fd);
  	fd = open("pmemfile_pool/pool1/bbb", O_CREAT, O_RDWR);
	fprintf(stderr, "fd for bbb %d\n", fd);
  	//ssize_t z = write(fd, msg, strlen(msg1) + 1);
	//fprintf(stderr, "write %lu bytes to %d\n", z, fd);
	close(fd);
  	fd = open("pmemfile_pool/pool1/bbb", O_CREAT, O_RDWR);
	ssize_t z = read(fd, buf, sizeof(buf) - 1);
	fprintf(stderr, "fd for bbb %d\n", fd);
	fprintf(stderr, "read %lu bytes to %d\n", z, fd);
	close(fd);
	exit(0);
}
