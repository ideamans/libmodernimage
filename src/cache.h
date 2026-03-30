/*
 * libmodernimage - Config cache
 *
 * Caches parsed tool configurations keyed by normalized argv.
 * Entries are tool-specific opaque blobs with a destructor.
 */

#ifndef MI_CACHE_H_
#define MI_CACHE_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mi_cache_dtor)(void* entry);

typedef struct mi_cache_entry {
    char*                    key;
    void*                    data;
    mi_cache_dtor            dtor;
    struct mi_cache_entry*   next;
} mi_cache_entry_t;

typedef struct {
    mi_cache_entry_t* head;
    int               count;
} mi_cache_t;

/* Initialize/destroy cache */
void mi_cache_init(mi_cache_t* cache);
void mi_cache_clear(mi_cache_t* cache);

/* Look up a cached entry. Returns NULL on miss. */
void* mi_cache_get(mi_cache_t* cache, const char* key);

/* Store an entry. Takes ownership of data; dtor called on eviction/clear. */
void mi_cache_put(mi_cache_t* cache, const char* key, void* data, mi_cache_dtor dtor);

/*
 * Generate a cache key from argv, excluding values at specified indices.
 * Caller must free() the returned string.
 *
 * tool_name: prefix for the key (e.g. "cwebp")
 * argv/argc: full argument array
 * skip_indices: array of indices to exclude (input/output paths)
 * skip_count: number of indices to skip
 */
char* mi_cache_make_key(const char* tool_name, int argc, const char* argv[],
                        const int* skip_indices, int skip_count);

#ifdef __cplusplus
}
#endif

#endif /* MI_CACHE_H_ */
