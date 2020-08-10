#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/synch.h"
/* A directory. */
struct dir
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
    struct lock dlock;
  };

/* A single directory entry. */
struct dir_entry
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
     // lock_init(&dir->dlock);
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}
/* Iterates through a string, checking each directory's existence
   Intended example: "/code/personal/hw5/src/"
                     ['code','personal,'hw5','src']
                     validate each and return src directory */
struct dir *get_dir_from(const char *name) {

  struct dir *d = get_start_from(name);
  char *token; char *zero = NULL;
  struct dir *nextdir; struct inode *inode_ = NULL;
  const char *case_current = ".";
  const char *case_parent = "..";
  //if split index = 0, only filename is passed in and we return either absolute or current dir
  if (get_splitIndex(name) == 0) {
    return d;
  }

  for (token = strtok_r(name,"/",&zero); token != NULL; token = strtok_r(NULL,"/",&zero)) {
    if (memcmp((const char*)token,case_current,1) == 0 && strlen(token) == 1) {
      //get current directory
      inode_ = inode_open(thread_current()->cdir_);
      nextdir = dir_open(inode_);
      dir_close(d);
      d = nextdir;
      continue;
    }
    if (memcmp((const char *)token,case_parent,2) == 0 && strlen(token) == 2) {
      //get parent directory
      inode_ = inode_open(thread_current()->pdir_);
      nextdir = dir_open(inode_);
      dir_close(d);
      d = nextdir;
      continue;
    }
    if (dir_lookup(d,token,&inode_) == false)
      return NULL;
    if (inode_get_isdir(inode_) == false)
      return NULL;
    nextdir = dir_open(inode_);
    dir_close(d);
    d = nextdir;
  }
  
  return d;
}
/* Returns the last token of a path to use as a filename
   Intended example: "/code/personal/hw5/src/somefile.c"
                    ignore ['code','personal,'hw5','src']
                    return 'somefile.c' */
const char *get_fname_from(const char *name) {
  char *token; char *zero = NULL; char *ret_token;
  for (token = strtok_r(name,"/",&zero); token != NULL; token = strtok_r(NULL,"/",&zero)) {
    ret_token = token;
  }
  return ret_token;
}

/* Checks first character of a string and returns either current directory or root directory */
struct dir *get_start_from(const char *name) {
  const char *check = "/";
  struct dir *d = thread_current()->cdir_;
  struct dir *e = dir_open_root();  
  if (memcmp(name,check,1) != 0)
    return dir_open_root();
  return dir_open(inode_open(thread_current()->cdir_));

}
/*denotes seperator between directory searching and filename (should be the last index of an array) */
int get_splitIndex(const char *name) {
  int index = 0; int i = 0;
  while (name[i] != '\0') {
    if (name[i] == '/' && i != 0)
      index += 1;
    i += 1;
  }
  return index;
}

/*seperate handling for split = 0, meant only to be used with chdir syscall */
bool chdir_to(const char *name) {
   struct dir *d = get_start_from(name);
   char *token; char *zero = NULL;
   struct dir *nextdir; struct inode *inode_ = NULL;
   const char *case_current = ".";
   const char *case_parent = "..";
   //if split index = 0, only filename is passed in and we return either absolute or current dir
   if (get_splitIndex(name) == 0) {
     if (dir_lookup(d,name,&inode_) == true && inode_get_isdir(inode_)) {
       nextdir = dir_open(inode_);
       dir_close(d);
       d = nextdir;
       thread_current()->pdir_ = thread_current()->cdir_;
       thread_current()->cdir_ = d;
       return true;
     }else {
       return false;
     }
   }

   for (token = strtok_r(name,"/",&zero); token != NULL; token = strtok_r(NULL,"/",&zero)) {
     if (memcmp((const char*)token,case_current,1) == 0 && strlen(token) == 1) {
       //get current directory
       inode_ = inode_open(thread_current()->cdir_);
       nextdir = dir_open(inode_);
       dir_close(d);
       d = nextdir;
       continue;
     }
     if (memcmp((const char *)token,case_parent,2) == 0 && strlen(token) == 2) {
       //get parent directory
       inode_ = inode_open(thread_current()->pdir_);
       nextdir = dir_open(inode_);
       dir_close(d);
       d = nextdir;
       continue;
     }
     if (dir_lookup(d,token,&inode_) == false)
       return false;
     if (inode_get_isdir(inode_) == false)
       return false;
     nextdir = dir_open(inode_);
     dir_close(d);
     d = nextdir;
   }
  thread_current()->pdir_ = thread_current()->cdir_;
  thread_current()->cdir_ = d;
  return true;
}
