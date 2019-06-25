#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/kernel/list.h"

typedef int mapid_t;

struct file_descriptor {
  int id;
  struct file * file;
  struct list_elem elem;
};

struct mmap_info {
  mapid_t id;
  struct file * file;
  void * start_addr;
  uint32_t length;

  struct list_elem elem;

};

void syscall_init (void);

void sys_munmap (mapid_t mapid);

#endif /* userprog/syscall.h */
