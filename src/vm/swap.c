#include <bitmap.h>
#include <threads/vaddr.h>
#include <threads/synch.h>
#include <devices/block.h>
#include <stdio.h>
#include <threads/thread.h>
#include "vm/swap.h"

static const size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

static struct block *swap_block;

/* Record whether a swap region is available or occupied. */
static struct bitmap *swap_map;

static size_t swap_size;

void
swap_init()
{
  lock_init(&swap_lock);
  swap_block = block_get_role(BLOCK_SWAP);
  if (swap_block == NULL)
    PANIC ("Can't get swap block");

  swap_size = block_size(swap_block) / SECTORS_PER_PAGE;
  swap_map = bitmap_create(swap_size);
  bitmap_set_all(swap_map, true);
}

sid_t
swap_out(void *upage)
{
  lock_acquire(&swap_lock);
  sid_t sid = (sid_t) bitmap_scan_and_flip(swap_map, 0, 1, true);
  if (sid == BITMAP_ERROR)
    PANIC ("Swap block is full");

  for (size_t i = 0; i < SECTORS_PER_PAGE; ++i)
    {
      block_write(swap_block, (block_sector_t) (sid * SECTORS_PER_PAGE + i),
                  ((char *) upage) + i * BLOCK_SECTOR_SIZE);
    }

  lock_release(&swap_lock);
  return sid;
}

void swap_in(sid_t sid, void *upage)
{
  lock_acquire(&swap_lock);
  ASSERT (sid < swap_size && !bitmap_test(swap_map, (size_t) sid));
  for (size_t i = 0; i < SECTORS_PER_PAGE; ++i)
    {
      block_read(swap_block, (block_sector_t) (sid * SECTORS_PER_PAGE + i),
                 ((char *) upage) + i * BLOCK_SECTOR_SIZE);
    }
  bitmap_set(swap_map, (size_t) sid, true);
  lock_release(&swap_lock);
}

void swap_free(sid_t sid)
{
  lock_acquire(&swap_lock);
  ASSERT (sid < swap_size && !bitmap_test(swap_map, (size_t) sid));
  bitmap_set(swap_map, (size_t) sid, true);
  lock_release(&swap_lock);
}