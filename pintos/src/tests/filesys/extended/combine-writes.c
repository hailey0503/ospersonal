/* Child process for syn-rw.
   Reads from a file created by our parent process, which is
   growing it.  We loop until we've read the whole file
   successfully.  Many iterations through the loop will return 0
   bytes, because the file has not grown in the meantime.  That
   is, we are "busy waiting" for the file to grow.
   (This test could be improved by adding a "yield" system call
   and calling yield whenever we receive a 0-byte read.) */

#include <random.h>
#include <stdlib.h>
#include <syscall.h>
#include "tests/lib.h"
#include "devices/block.h"

const char *test_name = "combine-writes";

/*64KB is twice the maximum allowed buffer cache size*/
char buf1[64000];
char buf2[64000];

void
test_main (int argc, const char *argv[])
{

  char* file_name = "example.txt";
  int fd;

  /* Checks that file opens. */
  CHECK ((fd = create(file_name)) > 1, "create \"%s\"", file_name);

  /* Large amount of bytes to add to file */
  random_bytes(buf1, sizeof(buf1));

  /* Writing bytes to file byte-by-byte*/
  while (write(fd, buf1, 1)) {};

  /* Read it in byte by byte */
  while (read(fd, buf2, 1)) {};

  /*Get the block devices*/
  unsigned long long write_count;
  struct list_elem *e;
  for (e = list_begin(&all_blocks); e != list_end(&all_blocks);
        e = list_next(e))
    {
      struct block *block = list_entry(e, struct block, list_elem);
      if (block_type(block) == BLOCK_FILESYS) {
        write_count += block->write_cnt;
      }
    }

  /*See if the write count is approximately 128 */
  CHECK (write_count > 150 | write_count < 100);

  close (fd);

}
