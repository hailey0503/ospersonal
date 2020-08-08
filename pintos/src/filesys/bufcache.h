#ifndef FILESYS_BUFCACHE_H
#define FILESYS_BUFCACHE_H

#include "devices/block.h"

void bufcache_init(void);
void bufcache_read(struct block *block, block_sector_t sector, void* buffer, size_t offset, size_t length);
void bufcache_write(struct block *block, block_sector_t sector, void* buffer, size_t offset, size_t length);
void bufcache_flush(void);

#endif
