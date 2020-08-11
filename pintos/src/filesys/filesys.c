#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);


/*Splits name into a valid directory and a new filename to create */
void set_items(struct passer_create *pc, int splitIndex, const char *name) {
  struct dir *d = get_start_from(name);
  char *token; char *zero = NULL; const char *cc = "/";
  struct dir *nextdir; struct inode *inode_ = NULL; int i = 0;
  
  //only fed a single filename, pass back starting directory & fname
  if (splitIndex == 0 && memcmp(name,cc,1) != 0) {
    pc ->retdir = d;
    pc->ret_name = name;
    return;
  }
  /*
  if (memcmp(name,cc,1) == 0 && splitIndex != 0)
    i += 1; 
  */

  for (token = strtok_r(name,"/",&zero); token != NULL; token = strtok_r(NULL,"/",&zero)) {
    if (splitIndex == 0) {
      break;
    }
    if (i == splitIndex) {
     break;
    }
    if (dir_lookup(d,token,&inode_) == false) {
      pc->retdir = NULL;
      pc->ret_name = NULL;
      return;
    }
    i += 1;
    nextdir = dir_open(inode_);
    dir_close(d);
    d = nextdir;
  }
  pc->retdir = d;
  pc->ret_name = token;
}

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool isdir_)
{
  /*Task 3 */
  //check if empty
  int sz = strlen(name);
  if (sz == 0)
    return false;
  //get the intended directory and new filename from *name
  struct passer_create *pc = malloc(sizeof(struct passer_create));
  int splitIndex = get_splitIndex(name);
  set_items(pc, splitIndex, name);
  struct dir *dir = pc->retdir;
  const char *fname = pc->ret_name;
  //get inode number of the directory we want to make new file at
  block_sector_t inode_sector = inode_get_inumber(dir_get_inode(dir));

  //block_sector_t inode_sector = 0;
  //struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir,fname /*name*/, inode_sector));
  if (!success && inode_sector != 0){
    free_map_release (inode_sector, 1);
  }
  //set isdir here somehow
  inode_set_isdir(inode_open(inode_sector),isdir_);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{


  //our implementation
  //check for empty files/directories
  int size = strlen(name);
  if (size == 0)
    return NULL;
  //copy name and get starting directory
  char *cpyname = malloc(sizeof(char) * (size + 1));
  strlcpy(cpyname, name, size+1);
  struct dir *d = get_start_from(name);

  //declare variables for strtok loop
  char *token; char *zero = NULL; struct dir *nextdir;
  struct inode *inode_ = NULL;
  const char *cc = "."; const char *cp = "..";

  if (size == 1) {
    inode_ = dir_get_inode(d);
    //dir_close(d);
    return file_open(inode_);
  }

  //strtok loop
  for (token = strtok_r(name, "/",&zero); token != NULL; token = strtok_r(NULL,"/",&zero)) {
    if (memcmp((const char*)token,cc,1) == 0 && strlen(token) == 1) {
      inode_ = dir_get_inode(thread_current()->cdir_);
      nextdir = dir_open(inode_);
      dir_close(d);
      d = nextdir;
      return file_open(inode_);
    }
    if (memcmp((const char *)token,cp,2) == 0 && strlen(token) == 2) {
      inode_ = dir_get_inode(thread_current()->pdir_);
      nextdir = dir_open(inode_);
      dir_close(d);
      d = nextdir;
      return file_open(inode_);
    }

    if (dir_lookup(d, token, &inode_) == false) {
      return NULL;
    }
    nextdir = dir_open(inode_);
    dir_close(d);
    d = nextdir;
  }
  free(cpyname);
 // dir_close(d);
  return file_open(inode_);

   
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  const char *cc = "/";
  if (memcmp((const char*)name,cc,1) == 0)
    return false;
  /*Task 3 */
  struct dir *dir = get_dir_from(name);
  const char *fname = get_fname_from(name);
  //struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir,fname);
  dir_close (dir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
