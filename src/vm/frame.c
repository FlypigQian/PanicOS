#include <hash.h>
#include <list.h>
#include <threads/thread.h>
#include <threads/synch.h>
#include <threads/malloc.h>
#include <threads/vaddr.h>
#include <userprog/pagedir.h>
#include <stdio.h>
#include "vm/frame.h"
#include "userprog/syscall.h"
#include "page.h"


struct frame_entry
{
  void *kpage;
  void *upage;
  struct thread *owner;
  bool pinned;

  struct hash_elem helem;
  struct list_elem lelem;
};

static struct hash frame_table;

static struct list frame_list;

static struct list_elem *hand_ptr = NULL;

static struct frame_entry* get_frame_entry(void *kpage);

static void set_pinned(void *kpage, bool pinned);

static struct frame_entry* next_frame_entry(void);

static struct frame_entry* pick_victim(void);

static unsigned frame_hash_func (const struct hash_elem *e, void *aux);

static bool frame_less_func (const struct hash_elem *a,
                             const struct hash_elem *b,
                             void *aux);

void
frame_init()
{
  lock_init(&frame_lock);
  hash_init(&frame_table, frame_hash_func, frame_less_func, NULL);
  list_init(&frame_list);
}

void*
allocate_frame(enum palloc_flags flags, void *upage)
{
  ASSERT (lock_held_by_current_thread(&frame_lock));

  void *kpage = palloc_get_page(flags | PAL_USER);
  if (kpage == NULL)
    {
#ifdef NOSWAP
      return NULL;
#else
      struct frame_entry *entry = pick_victim();
      ASSERT (entry != NULL);
      ASSERT (entry->owner != NULL);


      struct supp_entry *evicted = get_supp_entry(&(entry->owner->supp_page_table),
                                                  entry->upage);

      ASSERT (evicted != NULL);
      evict_page(evicted, entry->owner->pagedir);

      entry->upage = upage;
      entry->owner = thread_current();
      entry->pinned = true;

      return entry->kpage;
#endif
    }


  struct frame_entry *entry = malloc(sizeof(struct frame_entry));
  ASSERT (entry != NULL)

  entry->kpage = kpage;
  entry->upage = upage;
  entry->owner = thread_current();
  entry->pinned = true;
  struct hash_elem *prev = hash_insert(&frame_table, &entry->helem);
  ASSERT (prev == NULL);

  list_push_back(&frame_list, &entry->lelem);

  return kpage;
}

void
free_frame(void *kpage, bool free_page)
{
  ASSERT (lock_held_by_current_thread(&frame_lock));

  ASSERT (is_kernel_vaddr(kpage));
  ASSERT (pg_ofs(kpage) == 0);

  struct frame_entry *entry = get_frame_entry(kpage);
  hash_delete(&frame_table, &entry->helem);
  /* Removing frame_entry from frame_list may invalid hand_ptr.
   * On the other hand, freeing a frame means that the next
   * allocation wouldn't trigger eviction. Hence here I set
   * 'hand_ptr = NULL'. Next time of eviction would start from
   * list_begin(). */
  hand_ptr = NULL;
  list_remove(&entry->lelem);
  if (free_page) palloc_free_page(kpage);
  free(entry);
}

void
pin_frame(void *kpage)
{
  set_pinned(kpage, true);
}

void
unpin_frame(void *kpage)
{
  set_pinned(kpage, false);
}

void
acquire_frame_lock()
{
//  printf("[DEBUG]%d acquire frame_lock\n", thread_current()->tid);
  lock_acquire(&frame_lock);
//  printf("[DEBUG]%d get frame_lock\n", thread_current()->tid);
}

void
release_frame_lock()
{
  lock_release(&frame_lock);
}

static struct frame_entry*
next_frame_entry()
{
  ASSERT (!list_empty(&frame_list));

  if (hand_ptr == NULL || list_next(hand_ptr) == list_end(&frame_list))
    hand_ptr = list_begin(&frame_list);
  else
    hand_ptr = list_next(hand_ptr);

  struct frame_entry *entry = list_entry(hand_ptr, struct frame_entry, lelem);
  ASSERT (entry != NULL);
  return entry;
}

static struct frame_entry*
pick_victim()
{
  ASSERT (!hash_empty(&frame_table));

  size_t max = 2 * hash_size(&frame_table);
  for (size_t i = 0; i < max; ++i)
  {
    struct frame_entry *entry = next_frame_entry();
    if (entry->pinned)
      continue;
    ASSERT (entry->owner != NULL);
    uint32_t *pagedir = entry->owner->pagedir;
    bool accessed = pagedir_is_accessed(pagedir, entry->upage)
                    || pagedir_is_accessed(pagedir, entry->kpage);
    if (!accessed)
      return entry;

    pagedir_set_accessed(pagedir, entry->upage, false);
    pagedir_set_accessed(pagedir, entry->kpage, false);
  }

  PANIC ("Can't find a victim");
}

static struct frame_entry*
get_frame_entry(void *kpage)
{
  ASSERT (lock_held_by_current_thread(&frame_lock));

  ASSERT (is_kernel_vaddr(kpage));
  ASSERT (pg_ofs(kpage) == 0);

  struct frame_entry tmp;
  tmp.kpage = kpage;

  struct hash_elem *e = hash_find(&frame_table, &tmp.helem);
  if (e == NULL)
  {
    PANIC ("Can't find the kpage in frame_table");
  }

  return hash_entry(e, struct frame_entry, helem);
}

static void
set_pinned(void *kpage, bool pinned)
{
  ASSERT (lock_held_by_current_thread(&frame_lock));

  struct frame_entry *entry = get_frame_entry(kpage);
  ASSERT (entry->pinned != pinned);
  entry->pinned = pinned;
}

static unsigned
frame_hash_func (const struct hash_elem *e, void *aux)
{
  struct frame_entry *entry = hash_entry(e, struct frame_entry, helem);
  return hash_int((int)(entry->kpage));
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