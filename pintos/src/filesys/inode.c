#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/bufcache.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NUM_DIRECT 123
#define NUM_BLOCKS_IN_INDIRECT 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    //uint32_t isdir;
    bool isdir;
    block_sector_t direct_ptrs[NUM_DIRECT];
    block_sector_t singly_indirect_ptr;
    block_sector_t doubly_indirect_ptr;
    unsigned magic;                     /* Magic number. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    bool extending;
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock inode_lock;
    struct condition until_not_extending;
    struct condition until_no_writers;
    struct inode_disk data;             /* Inode content. */
  };

  struct indirect_block
  {
    block_sector_t blocks[NUM_BLOCKS_IN_INDIRECT];
  };


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode_disk *inode_disk, off_t pos)
{
  ASSERT (inode_disk != NULL);
    if (pos < inode_disk->length) {
      int block_num = pos / BLOCK_SECTOR_SIZE;
      if (block_num < NUM_DIRECT) {
        return inode_disk->direct_ptrs[block_num];
      }

      else if (block_num < NUM_DIRECT + NUM_BLOCKS_IN_INDIRECT) {
        struct indirect_block indirect;
        bufcache_read(fs_device, inode_disk->singly_indirect_ptr, &indirect, 0, BLOCK_SECTOR_SIZE);
        block_num -= NUM_DIRECT;
        return indirect.blocks[block_num];
      }

      else {
        struct indirect_block doubly_indirect;
        bufcache_read(fs_device, inode_disk->doubly_indirect_ptr, &doubly_indirect, 0, BLOCK_SECTOR_SIZE);
        block_num -= (NUM_DIRECT + NUM_BLOCKS_IN_INDIRECT);
        int indirect_block_num = block_num / NUM_BLOCKS_IN_INDIRECT;
        struct indirect_block indirect;
        bufcache_read(fs_device, doubly_indirect.blocks[indirect_block_num], &indirect, 0, BLOCK_SECTOR_SIZE);
        block_num = block_num % BLOCK_SECTOR_SIZE;
        return indirect.blocks[block_num];
      }

    } else {
      return -1;
    }
}

static bool allocate_file(struct inode *inode, struct inode_disk *inode_disk, off_t length) {
  size_t num_sectors = bytes_to_sectors(length);
  block_sector_t sector;
  size_t count;
  size_t block_num = 0;

  if (inode != NULL) {
    inode->extending = true;
    lock_release(&inode->inode_lock);
  }

  for (count = 0; count < NUM_DIRECT && count < num_sectors; count++) {
    if (byte_to_sector(inode_disk, block_num * BLOCK_SECTOR_SIZE) == -1) {
      if (!free_map_allocate(1, &sector)) {
        //deal with failure
        return false;
      }
      inode_disk->direct_ptrs[count] = sector;
    }
    block_num++;
  }
  num_sectors -= count;

  if (num_sectors > 0) {
    if (byte_to_sector(inode_disk, block_num * BLOCK_SECTOR_SIZE) == -1) {
      if (!free_map_allocate(1, &sector)) {
        // deal with failure
        return false;
      }
      inode_disk->singly_indirect_ptr = sector;
    }
    struct indirect_block indirect;
    bufcache_read(fs_device, inode_disk->singly_indirect_ptr, &indirect, 0, BLOCK_SECTOR_SIZE);
    for (count = 0; count < NUM_BLOCKS_IN_INDIRECT && count < num_sectors; count++) {
      if (byte_to_sector(inode_disk, block_num * BLOCK_SECTOR_SIZE) == -1) {
        if (!free_map_allocate(1, &sector)) {
          //deal with failure
          return false;
        }
        indirect.blocks[count] = sector;
      }
      block_num++;
    }
    bufcache_write(fs_device, inode_disk->singly_indirect_ptr, &indirect, 0, BLOCK_SECTOR_SIZE);
    num_sectors -= count;
  }

  if (num_sectors > 0) {
    if (byte_to_sector(inode_disk, block_num * BLOCK_SECTOR_SIZE) == -1) {
      if (!free_map_allocate(1, &sector)) {
        // deal with failure
        return false;
      }
      inode_disk->doubly_indirect_ptr = sector;
    }

    struct indirect_block doubly_indirect;
    bufcache_read(fs_device, inode_disk->doubly_indirect_ptr, &doubly_indirect, 0, BLOCK_SECTOR_SIZE);
    int num_indirect_blocks = DIV_ROUND_UP (num_sectors, BLOCK_SECTOR_SIZE);
    for (int i = 0; i < num_indirect_blocks; i++) {
      if (byte_to_sector(inode_disk, block_num * BLOCK_SECTOR_SIZE) == -1) {
        if (!free_map_allocate(1, &sector)) {
          // deal with failure
          return false;
        }
        doubly_indirect.blocks[i] = sector;
      }

      struct indirect_block indirect;
      bufcache_read(fs_device, doubly_indirect.blocks[i], &indirect, 0, BLOCK_SECTOR_SIZE);
      for (count = 0; count < NUM_BLOCKS_IN_INDIRECT && count < num_sectors; count++) {
        if (byte_to_sector(inode_disk, block_num * BLOCK_SECTOR_SIZE) == -1) {
          if (!free_map_allocate(1, &sector)) {
            //deal with failure
            return false;
          }
          indirect.blocks[count] = sector;
        }
        block_num++;
      }
      bufcache_write(fs_device, doubly_indirect.blocks[i], &indirect, 0, BLOCK_SECTOR_SIZE);
      num_sectors -= count;
    }
    bufcache_write(fs_device, inode_disk->doubly_indirect_ptr, &doubly_indirect, 0, BLOCK_SECTOR_SIZE);
  }

  if (inode != NULL) {
    lock_acquire(&inode->inode_lock);
    inode_disk->length = length;
    inode->extending = false;
    cond_broadcast(&inode->until_not_extending, &inode->inode_lock);
    lock_release(&inode->inode_lock);
  }

  return true;
}


static bool deallocate_file(struct inode *inode) {
  size_t num_sectors = bytes_to_sectors(inode->data.length);
  size_t count;
  for (count = 0; count < NUM_DIRECT && count < num_sectors; count++) {
    free_map_release (inode->data.direct_ptrs[count], 1);
  }
  num_sectors -= count;

  if (num_sectors > 0) {
    struct indirect_block indirect;
    bufcache_read(fs_device, inode->data.singly_indirect_ptr, &indirect, 0, BLOCK_SECTOR_SIZE);
    for (count = 0; count < NUM_BLOCKS_IN_INDIRECT && count < num_sectors; count++) {
      free_map_release (indirect.blocks[count], 1);
    }
    free_map_release(inode->data.singly_indirect_ptr, 1);
    num_sectors -= count;
  }

  if (num_sectors > 0) {
    struct indirect_block doubly_indirect;
    bufcache_read(fs_device, inode->data.doubly_indirect_ptr, &doubly_indirect, 0, BLOCK_SECTOR_SIZE);
    int num_indirect_blocks = DIV_ROUND_UP (num_sectors, BLOCK_SECTOR_SIZE);
    for (int i = 0; i < num_indirect_blocks; i++) {
      struct indirect_block indirect;
      bufcache_read(fs_device, doubly_indirect.blocks[i], &indirect, 0, BLOCK_SECTOR_SIZE);
      for (count = 0; count < NUM_BLOCKS_IN_INDIRECT && count < num_sectors; count++) {
        free_map_release (indirect.blocks[count], 1);
      }
      free_map_release(doubly_indirect.blocks[i], 1);
      num_sectors -= count;
    }
    free_map_release(inode->data.doubly_indirect_ptr, 1);
  }
  free_map_release(inode->sector, 1);
  return true;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init(&open_inodes_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->magic = INODE_MAGIC;
      if (allocate_file(NULL, disk_inode, length))
        {
          disk_inode->length = length;
          bufcache_write(fs_device, sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
          /*if (sectors > 0)
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;

              for (i = 0; i < sectors; i++)
                bufcache_write(fs_device, byte_to_sector (const struct inode *inode,i * BLOCK_SECTOR_SIZE) disk_inode->start + i, zeros, 0, BLOCK_SECTOR_SIZE);
            } */
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire(&open_inodes_lock);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          lock_release(&open_inodes_lock);
          return inode;
        }
    }
  lock_release(&open_inodes_lock);

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  lock_acquire(&open_inodes_lock);
  list_push_front (&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);

  cond_init(&inode->until_no_writers);
  cond_init(&inode->until_not_extending);
  lock_init(&inode->inode_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->extending = false;
  bufcache_read(fs_device, inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      bufcache_write(fs_device, inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          deallocate_file(inode);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  lock_acquire(&inode->inode_lock);
  while (inode->extending) {
    cond_wait(&inode->until_not_extending, &inode->inode_lock);
  }
  if (offset + size > inode->data.length) {
    lock_release(&inode->inode_lock);
    return -1;
  }
  lock_release(&inode->inode_lock);

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&inode->data, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      //block_read (fs_device, sector_idx, bounce);
      bufcache_read(fs_device, sector_idx, (void*) buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  lock_acquire(&inode->inode_lock);
  if (inode->deny_write_cnt > 0) {
    lock_release(&inode->inode_lock);
    return 0;
  }
  inode->deny_write_cnt--;
  lock_release(&inode->inode_lock);

  lock_acquire(&inode->inode_lock);
  while (inode->extending) {
    cond_wait(&inode->until_not_extending, &inode->inode_lock);
  }
  if (size + offset > inode->data.length) {
    allocate_file(inode, &inode->data, size + offset);
  } else {
      lock_release(&inode->inode_lock);
  }
  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&inode->data, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      //block_write (fs_device, sector_idx, bounce);
      bufcache_write(fs_device, sector_idx, (void*) buffer + bytes_written, sector_ofs, chunk_size);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  lock_acquire(&inode->inode_lock);
  inode->deny_write_cnt++;
  if (inode->deny_write_cnt == 0) {
    cond_broadcast(&inode->until_no_writers, &inode->inode_lock);
  }
  lock_release(&inode->inode_lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  lock_acquire(&inode->inode_lock);
  while (inode->deny_write_cnt < 0) {
      cond_wait(&inode->until_no_writers, &inode->inode_lock);
  }
  inode->deny_write_cnt++;
  lock_release(&inode->inode_lock);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
bool
inode_get_isdir ( struct inode *inode)
{
  return inode->data.isdir;
}

void
inode_set_isdir(struct inode *inode, bool value) {
 return inode->data.isdir;
}
