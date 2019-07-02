#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <filesys/off_t.h>
#include <lib/kernel/hash.h>
#include "vm/swap.h"

enum page_state
  {
    ON_FRAME,
    IN_SWAP,
    IN_FILE
  };

struct supp_entry
  {
    void *upage;

    struct hash_elem elem;

    enum page_state state;
    bool writable;

    void *kpage;                  /* Only valid when state == ON_FRAME. */

    sid_t sid;                    /* Only valid when state == IN_SWAP. */

    struct file * file;
    uint32_t offset;
    uint32_t read_bytes;

  };

void supp_page_table_init(struct hash *supp_page_table);

void supp_page_table_destroy(struct hash *supp_page_table);

bool set_supp_frame_entry(struct hash *supp_page_table,
                          void *upage, void *kpage, bool writable);

bool set_supp_mmap_entry(struct hash *supp_page_table, void *upage,
                         struct file *file, uint32_t offset, uint32_t read_bytes);

void unset_supp_mmap_entry(struct hash *supp_page_table, void *upage);

struct supp_entry* get_supp_entry(struct hash *supp_page_table, void *upage);

bool load_page(struct supp_entry *entry);

void evict_page(struct supp_entry *entry, uint32_t *pagedir);

#endif //VM_PAGE_H
