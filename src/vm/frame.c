#include <hash.h>
#include <list.h>
#include <threads/thread.h>
#include <threads/synch.h>
#include <threads/malloc.h>
#include <threads/vaddr.h>
#include "vm/frame.h"


static struct lock frame_lock;

static struct hash frame_table;

static unsigned frame_hash_func (const struct hash_elem *e, void *aux);

static bool frame_less_func (const struct hash_elem *a,
                             const struct hash_elem *b,
                             void *aux);

void
frame_init()
{
  lock_init(&frame_lock);
  hash_init(&frame_table, frame_hash_func, frame_less_func, NULL);
}

void*
allocate_frame(enum palloc_flags flags, void *upage)
{
  lock_acquire(&frame_lock);

  void *kpage = palloc_get_page(flags | PAL_USER);
  if (kpage == NULL)
    {
      PANIC ("Run out of user pool");
      // TODO: implement a page replacement algorithm
    }

  struct frame_entry *entry = malloc(sizeof(struct frame_entry));
  if (entry == NULL)
    {
      PANIC ("Run out of kernel pool");
    }

  entry->kpage = kpage;
  entry->upage = upage;
  entry->owner = thread_current();
  struct hash_elem *prev = hash_insert(&frame_table, &entry->helem);
  ASSERT (prev == NULL);

  lock_release(&frame_lock);

  return kpage;
}

void
free_frame(void *kpage, bool free_page)
{
  lock_acquire(&frame_lock);

  ASSERT (is_kernel_vaddr(kpage));
  ASSERT (pg_ofs(kpage) == 0);

  struct frame_entry tmp;
  tmp.kpage = kpage;

  struct hash_elem *e = hash_find(&frame_table, &tmp.helem);
  if (e == NULL)
    {
      PANIC ("Can't find the kpage in frame_table");
    }

  struct frame_entry *entry = hash_entry(e, struct frame_entry, helem);
  hash_delete(&frame_table, &entry->helem);
  if (free_page) palloc_free_page(kpage);
  free(entry);

  lock_release(&frame_lock);
}

static unsigned
frame_hash_func (const struct hash_elem *e, void *aux)
{
  struct frame_entry *entry = hash_entry(e, struct frame_entry, helem);
  return hash_bytes(&entry->kpage, sizeof(entry->kpage));
}

static bool
frame_less_func (const struct hash_elem *a,
                 const struct hash_elem *b,
                 void *aux)
{
  struct frame_entry *entry_a = hash_entry(a, struct frame_entry, helem);
  struct frame_entry *entry_b = hash_entry(b, struct frame_entry, helem);
  return entry_a->kpage < entry_b->kpage;
}