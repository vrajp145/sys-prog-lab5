#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
  // Validate the number of entries; it must be between 2 and 4096. Return -1 if invalid.
  if (num_entries < 2 || num_entries > 4096 || cache_enabled()) {
    return -1;
  }
  // Return -1 if cache is already initialized to prevent reinitialization.
  if (cache!=NULL){
    return -1;
  }
  // Allocate memory for the cache using calloc and update cache_size, then return 1 to indicate success.
  else{
    cache = calloc(num_entries, sizeof(cache_entry_t));
    cache_size=num_entries;
    return 1;
  }
}

int cache_destroy(void) {
  if (cache == NULL) {
    return -1; // Return -1 indicating failure as there's no cache to destroy.
  }
  free(cache); // Release the allocated memory for the cache.
  cache = NULL;
  cache_size=0;
  return 1; // Return 1 indicating successful destruction of the cache.
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
    // Increment the total number of queries made to the cache.
    num_queries++;
    
    // Early return if cache is not enabled, the buffer pointer is null, or indices are out of bounds.
    if (!cache_enabled() || buf == NULL || disk_num < 0 || disk_num >= 16 || block_num < 0 || block_num >= 256) {
        return -1;
    }

    // Search through the cache for an entry matching the disk and block numbers.
    for (int i = 0; i < cache_size; ++i) {
        if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
            // A matching entry has been found: copy its contents to the provided buffer.
            memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
            // Update this entry's last accessed time to maintain LRU order.
            cache[i].access_time = ++clock;
            // Record a successful hit.
            num_hits++;
            // Return success as the requested block was found and copied.
            return 1;
        }
    }

    // The requested block was not found in the cache.
    return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  if (cache == NULL || buf == NULL ) {
    return;
  }

  // Iterate through the cache to find an existing entry to update.
  for (int i = 0; i < cache_size; i++) {
    // Check if the current entry is valid and matches the disk and block numbers.
    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      // Update the access time of this cache entry and increment the global clock.
      clock++;
      cache[i].access_time = clock;
      cache[i].valid = true;
      return;
    }
  }

}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
    // Check cache state and input parameters.
    if (!cache_enabled() || buf == NULL) {
        return -1; // Return error if cache is not enabled or buffer is NULL.
    }

    // Validate disk number range.
    if (disk_num < 0 || disk_num > 15) {
        return -1; // Return error on invalid disk number.
    }

    // Check block number range.
    if (block_num < 0 || block_num > 255) {
        return -1; // Return error on invalid block number.
    }

    int least_LRU = 0; // Index of the least recently used (LRU) entry.
    // Iterate over cache entries to either find a matching entry or the LRU entry.

    // Iterate over cache entries to either find a matching entry or the LRU entry.
    for(int i = 0; i < cache_size; i++) {
        // Check if entry matches the given disk and block numbers.
        if (cache[i].valid && 
          cache[i].disk_num == disk_num && 
          cache[i].block_num == block_num) {
          return -1; // Entry already exists, return an error.
        }

        // Update least_LRU if this entry is less recently used than the current LRU.
        if (cache[i].access_time < cache[least_LRU].access_time) {
            least_LRU = i;
        }
    }

    // Update or insert the cache entry at the least recently used slot.
    cache[least_LRU].disk_num = disk_num;
    cache[least_LRU].block_num = block_num;
    cache[least_LRU].valid = true; // Mark the slot as valid.
    memcpy(cache[least_LRU].block, buf, 256);
    clock++;
    cache[least_LRU].access_time = clock; // Update access time to current clock, increment clock.

    return 1; // Successful insertion.
}

bool cache_enabled(void) {
  if (cache==NULL){
    return false;
  }
  return true; //Returns true if cache is enabled
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
