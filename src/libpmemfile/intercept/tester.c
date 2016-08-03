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
	fd = open("./aaa.sarah", O_CREAT, O_RDWR);
  	ssize_t w = write(fd, msg, strlen(msg));
	fprintf(stderr, "bytes written from %lu\n", w);
  	close(fd);
	fd = open("./aaa.sarah", O_CREAT, O_RDWR);
  	ssize_t r = read(fd, buf, sizeof(buf) - 1);
  	fprintf(stderr, "bytes read %lu %s\n", r, buf);
	close(fd);
  	fd = open("./aaa.foo1", O_CREAT, O_RDWR);
  	ssize_t z = write(fd, msg, strlen(msg1) + 1);
  	fprintf(stderr, "foo1 number of bytes written %lu\n", z);
	close(fd);
  	fd = open("./aaa.foo1", O_CREAT, O_RDWR);
	z = read(fd, buf, sizeof(buf) - 1);
	fprintf(stderr, "Bytes read from foo1 %s\n", buf);
	close(fd);
	exit(0);
}
