#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/kernel/list.h"

struct file_descriptor {
  int id;
  struct file* file;
  struct list_elem elem;
};

void syscall_init (void);

#endif /* userprog/syscall.h */
