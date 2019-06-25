#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <threads/palloc.h>
#include "lib/kernel/hash.h"

struct frame_entry
{
  void *kpage;
  void *upage;
  struct thread *owner;

  struct hash_elem helem;
};

void frame_init(void);

void* allocate_frame(enum palloc_flags flags, void *upage);

void free_frame(void *kpage, bool free_page);


#endif //VM_FRAME_H
