/* 
Test your buffer cache’s effectiveness by measuring its cache hit rate. 
First, reset the buffer cache. Open a file and read it sequentially, 
to determine the cache hit rate for a cold cache. 
Then, close it, re-open it, and read it sequentially again, to make sure that the cache hit rate improves.
*/
#include <stdio.h>
#include <syscall.h>
#include "filesys/bufcache.h"
#include "tests/lib.h"
#include "tests/main.h"
//#include "devices/block.h"

static void open_and_read(void);

static char buf [2048];
char *file_name = "sample.txt";


static void
open_and_read() {

  int fd = open (file_name);
  int bytes_read = 0;
  int buf_size = 0;

  while ((bytes_read = read(fd, &buf[buf_size], sizeof(buf) - buf_size)) > 0) { 
  
    msg("bytes_read %d", bytes_read);
    buf_size += bytes_read;
    msg("buf_size %d", buf_size);
  } 
  close(fd);

}


void
test_main (void)
{
  //struct block *fs_device = block_get_role (BLOCK_FILESYS);
  reset_bufcache();
  unsigned long long  read_count0 = block_get_rd();
  msg("0: %u\n",read_count0);
  open_and_read();
  unsigned long long  read_count1 = block_get_rd();
  msg("1: %u\n",read_count1);
  unsigned long long  diff_0 = read_count1 - read_count0;
  msg("diff0: %u\n",diff_0);
	open_and_read();
  unsigned long long  read_count2 = block_get_rd();
  msg("2: %u\n",read_count2);
  unsigned long long  diff_1 = read_count2 - read_count1;
  msg("diff1: %u\n",diff_1);

  if (diff_1 < diff_0) {
    msg("done");
  } else {
    msg("not done");
  }

}
