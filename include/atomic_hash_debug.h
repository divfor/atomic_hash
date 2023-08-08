/**
 * Optional header for testing the map implementation
 */
#ifndef ATOMIC_HASH_DEBUG_H
#define ATOMIC_HASH_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif


#include <unistd.h>
#include "atomic_hash.h"


void (*atomic_hash_debug_get_hash_func(hmap_t *h))(const void *key, size_t len, void *r);

void *atomic_hash_debug_get_teststr(hmap_t *hmap);
void atomic_hash_debug_set_teststr(hmap_t *hmap, void *teststr);

unsigned long atomic_hash_debug_get_teststr_num(hmap_t *hmap);
void atomic_hash_debug_set_teststr_num(hmap_t *hmap, unsigned long teststr_num);


#ifdef __cplusplus
}
#endif

#endif /* ATOMIC_HASH_DEBUG_H */
