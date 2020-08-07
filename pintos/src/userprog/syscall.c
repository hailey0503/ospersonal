#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"

static struct lock file_lock;
static struct global_file* global_files[1024];
static void syscall_handler (struct intr_frame *);
void validate_string( char* file, struct intr_frame *f);
void system_exit(struct intr_frame *f, int retval);
int syscall_practice(int i);
void syscall_halt (void);
tid_t syscall_exec (void *file);
int syscall_wait (tid_t tid);
int syscall_write (int fd, const void *buffer, unsigned size);
bool syscall_create (const char *file, unsigned initial_size);
bool syscall_remove (const char *file);
int syscall_open (const char *file);
int syscall_filesize (int fd);
int syscall_read (int fd, void *buffer, unsigned size);
void syscall_seek (int fd, unsigned position);
unsigned syscall_tell (int fd);
void syscall_close (int fd);
struct file* search_fd (struct list* list, int value);
struct global_file* search_global(struct file* file);
struct global_file* insert_global(struct file* file);
void delete_fd (struct list* list, int value);
void validate_pointer (void* pointer, struct intr_frame *f);

/* Free all the allocated memory that was assigned to a thread */
void free_thread(void) {
  lock_acquire(&file_lock);
  struct list *list = &(thread_current()->fds);
  struct list_elem *e;
  struct file_descriptor *fd;
  while (!list_empty(list)) {
    e = list_pop_front(list);
    fd = list_entry(e, struct file_descriptor, elem);
    if (fd->global_file != NULL) {
      delete_global(fd->global_file->file);
    }
    free(fd);
  }
  lock_release(&file_lock);
}

/* Helper function to find a given file in a process's list of file descriptors. */
struct file* search_fd (struct list* list, int value) {
  struct list_elem *e;
  for (e = list_begin(list); e != list_end(list); e = list_next(e)) {
    struct file_descriptor *fd = list_entry(e, struct file_descriptor, elem);
    if (fd->value == value) {
      return fd->global_file->file;
    }
  }
  return NULL;
}

/* Helper function to delete a given file descriptor from a process's list of file descriptors. */
void delete_fd (struct list* list, int value) {
  struct list_elem *e;
  for (e = list_begin(list); e != list_end(list); e = list_next(e)) {
    struct file_descriptor *fd = list_entry(e, struct file_descriptor, elem);
    if (fd->value == value) {
      list_remove(&fd->elem);
      free(fd);
      return;
    }
  }
}

/* Given a file, we search the global array of global files for a given global file */
struct global_file* search_global(struct file* file) {
  for (int i = 0; i < 1024; i++) {
    if (global_files[i] != NULL && global_files[i]->file == file) {
      return global_files[i];
    }
  }
  return NULL;
}

/* Given a file, we create a global file struct and put it into the global_file array at
the lowest available index */
struct global_file* insert_global(struct file* file) {
  for (int i = 0; i < 1024; i++) {
    if (global_files[i] == NULL) {
      struct global_file *gfile = malloc(sizeof(struct global_file));
      gfile->refcount = 1;
      gfile->file = file;
      global_files[i] = gfile;
      return gfile;
    }
  }
  return NULL;
}

/* Deleting a global file from the global array of global files. If there are
multiple process's pointing to a given file, then we decrement the reference count.
If there is only one file pointing to the file, then we deallocate the memory used for the
global file */
void delete_global(struct file* file) {
  struct global_file* gfile = search_global(file);
    if (gfile->refcount == 1) {
    free(gfile);
  } else if (gfile->refcount > 1) {
    gfile->refcount--;
  }
  return;
}

/* Helper function to validate addresses. If address is invalid, then call system_exit()
with return code -1. If valid, return nothing */
void validate_pointer (void* pointer, struct intr_frame *f) {
  if (pointer == NULL) {
    system_exit(f, -1);
    return;
  }
  if (!is_user_vaddr(pointer) || !is_user_vaddr(pointer + 3)) {
      system_exit(f, -1);
      return;
  } if (pagedir_get_page(thread_current()->pagedir, pointer) == NULL ||
      pagedir_get_page(thread_current()->pagedir, pointer + 3) == NULL ) {

    system_exit(f, -1);
    return;
  }
  return;
}

/* Helper function to check if a string is null terminated. If it is then return nothing
If it is not null terminated, call system_exit() with return code -1. */
void validate_string (char *file, struct intr_frame *f) {
  void* pointer = file;
  //max string length is 512
  for (unsigned int i = 0; i < 512; i++) {
      validate_pointer(pointer++, f);
      if (file[i] == '\0') {
            return;
      }
  }
  system_exit(f, -1);
  return;
}

void
syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}


static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  //printf("System call number: %d\n", args[0]);

  /* Validate pointer */
  validate_pointer(&args[0], f);
  int n = 1;
  if (args[0] == SYS_HALT) {
      n = 0;
  } else if (args[0] == SYS_CREATE || args[0] == SYS_SEEK || args[0] == SYS_MMAP
         || args[0] == SYS_READDIR) {
      n = 2;
  } else if (args[0] == SYS_READ || args[0] == SYS_WRITE)  {
      n = 3;
  }
  for (int i = 1; i <= n; i++) {
      validate_pointer(&args[i], f);
  }

  /* Do system call */
  if (args[0] == SYS_PRACTICE) {
      f->eax = syscall_practice(args[1]);

  } else if (args[0] == SYS_HALT) {
      syscall_halt();

  } else if (args[0] == SYS_EXIT) {
      system_exit(f, args[1]);

  }  else if (args[0] == SYS_EXEC) {
      validate_string ((char *)args[1], f);
      f->eax = syscall_exec((void *)args[1]);

  } else if (args[0] == SYS_WAIT) {
      f->eax = syscall_wait(args[1]);

  } else if (args[0] == SYS_CREATE) {

      validate_string((char*) args[1], f);

      char *file = pagedir_get_page(thread_current()->pagedir, (void*) args[1]);

      lock_acquire(&file_lock);
      f->eax = syscall_create(file, args[2]);
      lock_release(&file_lock);


  } else if (args[0] == SYS_REMOVE) {
      validate_string((char*) args[1], f);

      char *file = pagedir_get_page(thread_current()->pagedir, (void*) args[1]);

      lock_acquire(&file_lock);
      f->eax = syscall_remove(file);
      lock_release(&file_lock);


  } else if (args[0] == SYS_OPEN) {
      validate_string((char*) args[1], f);

      char *file = pagedir_get_page(thread_current()->pagedir, (void*) args[1]);

      lock_acquire(&file_lock);
      f->eax = syscall_open(file);
      lock_release(&file_lock);

  } else if (args[0] == SYS_FILESIZE) {

      lock_acquire(&file_lock);
      f->eax = syscall_filesize((int) args[1]);
      lock_release(&file_lock);


  } else if (args[0] == SYS_READ) {
      int fd = args[1];
      void *buffer = (void*) args[2];
      unsigned size = args[3];

      validate_pointer(buffer, f);
      validate_pointer(buffer + size - 4, f);

      buffer = pagedir_get_page(thread_current()->pagedir, buffer);

      lock_acquire(&file_lock);
      f->eax = syscall_read(fd, buffer, size);
      lock_release(&file_lock);


  } else if (args[0] == SYS_WRITE) {
      int fd = args[1];
      void *buffer = (void*) args[2];
      unsigned size = args[3];

      validate_pointer(buffer, f);
      validate_pointer(buffer + size - 4, f);

      buffer = pagedir_get_page(thread_current()->pagedir, buffer);

      lock_acquire(&file_lock);
      f->eax = syscall_write(fd, buffer, size);
      lock_release(&file_lock);

  } else if (args[0] == SYS_SEEK) {

      lock_acquire(&file_lock);
      syscall_seek(args[1], args[2]);
      lock_release(&file_lock);

  } else if (args[0] == SYS_TELL) {

      lock_acquire(&file_lock);
      f->eax = syscall_tell(args[1]);
      lock_release(&file_lock);

  } else if (args[0] == SYS_CLOSE) {
    int fd = args[1];
    if (fd == 0 || fd == 1 || fd < 0 || fd > 128 || search_fd(&thread_current()->fds, fd) == NULL) {
          system_exit(f, -1);
          return;
    }
    lock_acquire(&file_lock);
    syscall_close(fd);
    lock_release(&file_lock);

  } else {
      system_exit(f, -1);
  }
}

int syscall_practice (int i) {
    return i + 1;
}

void syscall_halt (void) {
  shutdown_power_off();
}

void system_exit(struct intr_frame *f, int retval) {
    f->eax = retval;
    thread_current()->rvalue = retval;
    thread_exit ();
}

tid_t syscall_exec (void *file) {
    const char *file_name = pagedir_get_page(thread_current()->pagedir, file);
    tid_t tid = process_execute(file_name);
    if (tid == TID_ERROR) {
      return -1;
    }
    struct list_elem *e;
    process_share *p;
    for (e = list_begin(&thread_current()->child_share);
        e != list_end(&thread_current()->child_share); e = list_next(e)) {
        p = list_entry(e, process_share, elem);
        if (p->tid == tid) {
            break;
        }
    }
    sema_down(&p->successload);
    return p->loaded ? tid : -1;
}

int syscall_wait(tid_t tid) {
    return process_wait(tid);
}

bool syscall_create (const char *file, unsigned initial_size) {
  bool retval = filesys_create(file, initial_size);
  return retval;
}

bool syscall_remove (const char *file) {
  bool retval = filesys_remove(file);
  return retval;
}

int syscall_open (const char *file) {
  struct file *curr_file = filesys_open(file);
  if (curr_file == NULL) {
    return -1;
  }
    /* Find lowest available fd */
    int fd = 2;
    while (thread_current()->closed_files[fd] && fd <= 128) {
        fd++;
    }

    thread_current()->closed_files[fd] = 1;

    struct global_file *gfile = search_global(curr_file);
    if (gfile == NULL) {
      gfile = insert_global(curr_file);
    }

    if (gfile == NULL){
      return -1;
    }

    /* Add fd to the thread's list of file descriptors */
    struct file_descriptor *file_des = malloc(sizeof(struct file_descriptor)); //remember to free
    if (file_des == NULL) {
        return -1;
    }
    file_des->value = fd;
    file_des->global_file = gfile;
    list_push_back (&thread_current()->fds, &file_des->elem);

    return fd;
}

int syscall_filesize (int fd) {
  struct file *file = search_fd(&thread_current()->fds, fd);
  if (file == NULL) {
    return -1;
  }
  int retval = (int) file_length(file);
  return retval;
}

int syscall_read (int fd, void *buffer, unsigned size) {
  struct file* file;
  if (fd < 0 || fd > 128 || fd == 1 || (file = search_fd(&thread_current()->fds, fd)) == NULL) {
        return -1;
  }

  if (fd == 0) {
    for (unsigned i = 0; i < size; i++) {
        *(uint32_t*) buffer = input_getc();
        buffer++;
    }
    return size;
  }

  int retval = file_read(file, buffer, size);

  return retval;
}

int syscall_write (int fd, const void *buffer, unsigned size) {
    int retval;
    struct file* file;
    /* stdout, write to console */
    if (fd == 1) {
        putbuf(buffer, size);
        retval = size;
    } else if (fd <= 0 || fd > 128 || (file = search_fd(&thread_current()->fds, fd)) == NULL) {
         return 0;
    } else {
        retval = file_write(file, buffer, size);
    }

    return retval;
}

void syscall_seek (int fd, unsigned position) {
  struct file *file = search_fd(&thread_current()->fds, fd);
  if (file == NULL) {
    return;
  }
  file_seek(file, position);
}

unsigned syscall_tell (int fd) {
  struct file *file = search_fd(&thread_current()->fds, fd);
  if (file == NULL) {
    return -1;
  }
  unsigned retval = (unsigned) file_tell(file);
  return retval;
}

void syscall_close (int fd) {
    struct file *file = search_fd(&thread_current()->fds, fd);
    if (file == NULL) {
      return;
    }
    delete_fd(&thread_current()->fds, fd);
    thread_current()->closed_files[fd] = 1;
    delete_global(file);
    file_close(file);
  }
