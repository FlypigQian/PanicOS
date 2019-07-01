#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <threads/palloc.h>
#include "lib/kernel/hash.h"
#include "threads/synch.h"

struct lock frame_lock;

void frame_init(void);

void* allocate_frame(enum palloc_flags flags, void *upage);

void free_frame(void *kpage, bool free_page);

void pin_frame(void *kpage);

void unpin_frame(void *kpage);

void acquire_frame_lock();

void release_frame_lock();

#endif //VM_FRAME_H
