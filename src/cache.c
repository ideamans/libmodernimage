/*
 * libmodernimage - Config cache implementation
 */

#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mi_cache_init(mi_cache_t* cache) {
    cache->head = NULL;
    cache->count = 0;
}

void mi_cache_clear(mi_cache_t* cache) {
    mi_cache_entry_t* e = cache->head;
    while (e) {
        mi_cache_entry_t* next = e->next;
        if (e->dtor && e->data) e->dtor(e->data);
        free(e->key);
        free(e);
        e = next;
    }
    cache->head = NULL;
    cache->count = 0;
}

void* mi_cache_get(mi_cache_t* cache, const char* key) {
    for (mi_cache_entry_t* e = cache->head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e->data;
    }
    return NULL;
}

void mi_cache_put(mi_cache_t* cache, const char* key, void* data, mi_cache_dtor dtor) {
    /* Replace existing entry with same key */
    for (mi_cache_entry_t* e = cache->head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            if (e->dtor && e->data) e->dtor(e->data);
            e->data = data;
            e->dtor = dtor;
            return;
        }
    }
    /* New entry */
    mi_cache_entry_t* e = calloc(1, sizeof(mi_cache_entry_t));
    if (!e) return;
    e->key = strdup(key);
    e->data = data;
    e->dtor = dtor;
    e->next = cache->head;
    cache->head = e;
    cache->count++;
}

char* mi_cache_make_key(const char* tool_name, int argc, const char* argv[],
                        const int* skip_indices, int skip_count) {
    /* Calculate total length */
    size_t len = strlen(tool_name) + 1;
    for (int i = 0; i < argc; i++) {
        int skip = 0;
        for (int j = 0; j < skip_count; j++) {
            if (skip_indices[j] == i) { skip = 1; break; }
        }
        if (!skip && argv[i]) {
            len += strlen(argv[i]) + 1;
        }
    }

    char* key = malloc(len + 1);
    if (!key) return NULL;

    char* p = key;
    p += sprintf(p, "%s", tool_name);
    for (int i = 0; i < argc; i++) {
        int skip = 0;
        for (int j = 0; j < skip_count; j++) {
            if (skip_indices[j] == i) { skip = 1; break; }
        }
        if (!skip && argv[i]) {
            *p++ = '\0';  /* Use NUL as separator to avoid ambiguity */
            p += sprintf(p, "%s", argv[i]);
        }
    }
    /* Replace NUL separators with '|' for a readable key */
    for (size_t i = 0; i < (size_t)(p - key); i++) {
        if (key[i] == '\0') key[i] = '|';
    }
    *p = '\0';
    return key;
}
