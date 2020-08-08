#include "filesys/bufcache.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include <list.h>
#include <stdbool.h>
#include <debug.h>
#include <string.h>

#define NUM_ENTRIES 64

struct data {
  unsigned char contents[BLOCK_SECTOR_SIZE];
};

struct metadata {
  block_sector_t sector;
  struct data* entry;
  struct list_elem lru_elem;
  struct condition until_ready;
  bool ready;
  bool dirty;
};

static struct metadata entries[NUM_ENTRIES];
static struct data cached_data[NUM_ENTRIES];


static struct lock cache_lock;
static struct condition until_one_ready;
static struct list lru_list;

void bufcache_init(void) {
  lock_init(&cache_lock);
  list_init(&lru_list);
  cond_init(&until_one_ready);
  for (int i = 0; i < NUM_ENTRIES; i++) {
    cond_init(&entries[i].until_ready);
    entries[i].dirty = false;
    entries[i].ready = true;
    entries[i].sector = -1; //INVALID SECTOR
    entries[i].entry = &cached_data[i];
    list_push_front(&lru_list, &entries[i].lru_elem);
  }
}

static struct metadata* get_eviction_candidate(void) {
  ASSERT(lock_held_by_current_thread(&cache_lock));
  struct list_elem* e;
  for (e = list_rbegin(&lru_list); e != list_rend(&lru_list); e = list_prev(e)) {
    struct metadata* meta = list_entry(e, struct metadata, lru_elem);
    if (meta->ready == true) {
      return meta;
    }
  }
  return NULL;
}

static struct metadata* find(block_sector_t sector) {
    for (int i = 0; i < NUM_ENTRIES; i++) {
      if (sector == entries[i].sector) {
        return &entries[i];
      }
    }
    return NULL;
}
static void clean(struct block *block, struct metadata* entry) {
    ASSERT(lock_held_by_current_thread(&cache_lock));
    ASSERT(entry->dirty);
    entry->ready = false;
    lock_release(&cache_lock);
    block_write(block, entry->sector, (void*) entry->entry);
    lock_acquire(&cache_lock);
    entry->ready = true;
    entry->dirty = false;
    cond_broadcast(&entry->until_ready, &cache_lock);
    cond_broadcast(&until_one_ready, &cache_lock);
}

static void replace(struct block *block, struct metadata* entry, block_sector_t sector) {
  ASSERT(lock_held_by_current_thread(&cache_lock));
  ASSERT(!entry->dirty);
  entry->sector = sector;
  entry->ready = false;
  lock_release(&cache_lock);
  block_read(block, sector, (void*) entry->entry);
  lock_acquire(&cache_lock);
  entry->ready = true;
  cond_broadcast(&entry->until_ready, &cache_lock);
  cond_broadcast(&until_one_ready, &cache_lock);
}

static struct metadata* bufcache_access(struct block *block, block_sector_t sector) {
  ASSERT(lock_held_by_current_thread(&cache_lock));
  while(1) {
    struct metadata* match = find(sector);

    if (match != NULL) {
      if (!match->ready) {
        cond_wait(&match->until_ready, &cache_lock);
        continue;
      }

      list_remove(&match->lru_elem);
      list_push_front(&lru_list, &match->lru_elem);

      return match;
    }

    struct metadata* to_evict = get_eviction_candidate();

    if (to_evict == NULL) {
      cond_wait(&until_one_ready, &cache_lock);
    }

    else if (to_evict->dirty) {
      clean(block, to_evict);
    }

    else {
      replace(block, to_evict, sector);
    }
  }
}

void bufcache_read(struct block *block, block_sector_t sector, void* buffer, size_t offset, size_t length) {
  ASSERT(offset + length <= BLOCK_SECTOR_SIZE);
  lock_acquire(&cache_lock);
  struct metadata* entry = bufcache_access(block, sector);
  memcpy(buffer, &entry->entry->contents[offset], length);
  lock_release(&cache_lock);
}

void bufcache_write(struct block *block, block_sector_t sector, void* buffer, size_t offset, size_t length) {
  ASSERT(offset + length <= BLOCK_SECTOR_SIZE);
  lock_acquire(&cache_lock);
  struct metadata* entry = bufcache_access(block, sector);
  memcpy(&entry->entry->contents[offset], buffer, length);
  entry->dirty = true;
  lock_release(&cache_lock);
}

void bufcache_flush(void) {
  lock_acquire(&cache_lock);
  for (int i = 0; i < NUM_ENTRIES; i++) {
    if (entries[i].dirty && entries[i].ready) {
      clean(fs_device, &entries[i]);
    }
  }
}
