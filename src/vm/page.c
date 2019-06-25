#include <threads/malloc.h>
#include <userprog/pagedir.h>
#include <threads/thread.h>
#include <filesys/file.h>
#include <stdio.h>
#include <threads/synch.h>
#include "vm/page.h"
#include "frame.h"

extern struct lock fs_lock;

static unsigned supp_hash_func (const struct hash_elem *e, void *aux);

static bool supp_less_func (const struct hash_elem *a,
                            const struct hash_elem *b,
                            void *aux);

static void supp_destroy_func (struct hash_elem *e, void *aux);

void
supp_page_table_init(struct hash *supp_page_table)
{
  hash_init(supp_page_table, supp_hash_func, supp_less_func, NULL);
}

void
supp_page_table_destroy(struct hash *supp_page_table)
{
  hash_destroy(supp_page_table, supp_destroy_func);
}

bool
set_supp_frame_entry(struct hash *supp_page_table,
                     void *upage, void *kpage, bool writable)
{
  struct supp_entry *entry = (struct supp_entry *) malloc(sizeof(struct supp_entry));

  entry->upage = upage;
  entry->state = ON_FRAME;
  entry->writable = writable;
  entry->kpage = kpage;
  entry->sid = -1;
  entry->file = NULL;

  struct hash_elem *prev = hash_insert(supp_page_table, &entry->elem);
  if (prev == NULL)
      return true;
  else
    {
      free(entry);
      return false;
    }
}

bool
set_supp_mmap_entry(struct hash *supp_page_table, void *upage,
                    struct file *file, uint32_t offset, uint32_t read_bytes)
{
  struct supp_entry *entry = (struct supp_entry *) malloc(sizeof(struct supp_entry));

  entry->upage = upage;
  entry->state = IN_FILE;
  entry->writable = true;
  entry->kpage = NULL;
  entry->sid = -1;
  entry->file = file;
  entry->offset = offset;
  entry->read_bytes = read_bytes;

  struct hash_elem *prev = hash_insert(supp_page_table, &entry->elem);
  if (prev == NULL)
    return true;
  else
    {
      free(entry);
      return false;
    }
}

void
unset_supp_mmap_entry(struct hash *supp_page_table, void *upage)
{
  struct supp_entry *entry = get_supp_entry(supp_page_table, upage);

  ASSERT (entry != NULL);
  ASSERT (entry->file != NULL);
  ASSERT (entry->state != IN_SWAP);

  if (entry->state == ON_FRAME)
    {
      ASSERT (entry->kpage != NULL);

      struct thread *cur = thread_current();
      bool dirty = pagedir_is_dirty(cur->pagedir, upage)
              || pagedir_is_dirty(cur->pagedir, entry->kpage);
      if (dirty)
        {
          file_write_at(entry->file, upage, entry->read_bytes, entry->offset);
        }

      free_frame(entry->kpage, true);
      pagedir_clear_page(cur->pagedir, upage);
    }

  hash_delete(supp_page_table, &entry->elem);
}


struct supp_entry*
get_supp_entry(struct hash *supp_page_table, void *upage)
{
  struct supp_entry tmp;
  tmp.upage = upage;
  struct hash_elem *e = hash_find(supp_page_table, &tmp.elem);
  if (e == NULL)
    return NULL;
  return hash_entry(e, struct supp_entry, elem);
}

bool
load_page(struct supp_entry *entry)
{
  ASSERT (entry->state != ON_FRAME);

  if (entry->state == IN_FILE)
    {
      void *kpage = allocate_frame(PAL_USER, entry->upage);
      if (kpage == NULL)
        return false;

      entry->state = ON_FRAME;
      entry->kpage = kpage;

      ASSERT (entry->sid == -1);
      ASSERT (entry->file != NULL);

      struct thread *cur = thread_current();
      pagedir_set_page(cur->pagedir, entry->upage, kpage, entry->writable);

      lock_acquire(&fs_lock);
      file_read_at(entry->file, kpage, entry->read_bytes, entry->offset);
      lock_release(&fs_lock);

      pagedir_set_dirty(cur->pagedir, kpage, false);

      return true;
    }

  if (entry->state == IN_SWAP)
    {
      /* TODO */
    }

  return false;
}

static unsigned
supp_hash_func (const struct hash_elem *e, void *aux)
{
  struct supp_entry *entry = hash_entry(e, struct supp_entry, elem);
  return hash_bytes(&entry->upage, sizeof(entry->upage));
}

static bool
supp_less_func (const struct hash_elem *a,
                const struct hash_elem *b,
                void *aux)
{
  struct supp_entry *entry_a = hash_entry(a, struct supp_entry, elem);
  struct supp_entry *entry_b = hash_entry(b, struct supp_entry, elem);
  return entry_a->upage < entry_b->upage;
}

static void
supp_destroy_func (struct hash_elem *e, void *aux)
{
  struct supp_entry *entry = hash_entry(e, struct supp_entry, elem);

  ASSERT (entry->state != IN_FILE);

  if (entry->state == ON_FRAME)
    {
      free_frame(entry->kpage, false);
    }

  if (entry->state == IN_SWAP)
    {
      /* TODO */
    }

  free(entry);
}
