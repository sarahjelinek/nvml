/*
 * trivial program for testing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char *argv[])
{
  fprintf(stderr, "Hello from main \n");
  char *msg = "Hello World"; 
  char *msg1 = "Hello World Again"; 
  char buf[8192];

	FILE *fp = fopen("./foo", "w+");
  size_t w = fwrite(msg, 1, strlen(msg) + 1, fp);
  fprintf(stderr, "bytes written %lu\n", w);
  fseek(fp, 0, SEEK_SET);
  size_t r = fread(buf, 1, sizeof(buf) - 1, fp);
  fprintf(stderr, "number of bytes read %lu\n", r);
  fprintf(stderr, "bytes read %s\n", buf);
  fclose(fp);
  fp = fopen("/etc/passwd", "r");
  r = fread(buf, 1, sizeof(buf) - 1, fp);
  fprintf(stderr, "Reading from /etc/passwd %s\n", buf);
	fclose(fp);
  fp = fopen("./foo1", "w+");
  size_t z = fwrite(msg1, 1, strlen(msg1) + 1, fp);
  fseek(fp, 0, SEEK_SET);
  fprintf(stderr, "./foo1 number of bytes written %lu\n", z);
  z = fread(buf, 1, sizeof(buf) - 1, fp);
  fprintf(stderr, "Number of bytes reasd from ./foo1 %lu\n", z);
  fprintf(stderr, "Bytes read from ./foo1 %s\n", buf);
  fclose(fp);
	exit(0);
}
