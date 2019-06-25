#include <threads/malloc.h>
#include "vm/page.h"
#include "frame.h"

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
set_supp_entry(struct hash *supp_page_table, void *upage, void *kpage)
{
  struct supp_entry *entry = (struct supp_entry *) malloc(sizeof(struct supp_entry));

  entry->upage = upage;
  entry->kpage = kpage;
  entry->state = ON_FRAME;

  struct hash_elem *prev = hash_insert(supp_page_table, &entry->elem);
  if (prev == NULL)
      return true;
  else
    {
      free(entry);
      return false;
    }
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
  if (entry->state == ON_FRAME)
    {
      free_frame(entry->kpage, false);
    }
  free(entry);
}
