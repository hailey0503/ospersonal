

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
}

#define NUM_ENTRIES 64
static struct data cached_data[NUM_ENTRIES];
static struct metdadata entries[NUM_ENTRIES];
static struct lock cache_lock;
static struct condition until_one_ready;
static struct list lru_list;


static struct metadata* get_eviction_candidate(void) {

}

static struct metdata* find(block_sector_t sector) {

}
static void clean(struct metadata* entry) {

}
static void replace(struct metadata* entry, block_sector_t sector) {

}
static struct metadata* bufcache_access(block_sector_t sector) {

}


void bufcache_init(void) {
  lock_init(&cache_lock);
  list_init(&lru_list);
  cond_init(&until_one_ready);

}

void bufcache_read(block_sector_t sector, void* buffer, size_t offset, size_t length) {

}
void bufcache_write(block_sector_t sector, void* buffer, size_t offset, size_t length) {

}
void bufcache_flush(void) {

}
