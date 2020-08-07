#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/kernel/list.h"

struct global_file {
    int refcount;
    struct file* file;
};

struct file_descriptor {
    struct list_elem elem;      /* List element. */
    int value;                  /* fd value. */
    struct global_file* global_file; /* pointer to the file */
};

void syscall_init (void);
void free_thread(void);
void delete_global(struct file* file);

#endif /* userprog/syscall.h */
