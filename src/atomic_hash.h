/*
 * 2012-2015 Copyright (c)
 * Fred Huang, <divfor@gmail.com>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef ATOMIC_HASH_H
#define ATOMIC_HASH_H

/* -- Consts -- */
/* callback function idx */
#define HOOK_NODE_REMOVE     -1
#define HOOK_TTL_RESET       -2
#define HOOK_TTL_DONT_CHANGE -3
#define HOOK_TTL_SET_TO(n)   (n)


/* -- Types -- */
typedef struct hmap hmap_t;

typedef int (*hook_t) (void *hash_data, void *rtn_data);


/* -- Function prototypes -- */
/* For documentation, see README.md */
hmap_t *atomic_hash_create (unsigned int max_nodes, int reset_ttl);
void atomic_hash_register_hooks(hmap_t *hmap,
                                hook_t cb_on_ttl, hook_t cb_on_add, hook_t cb_on_dup, hook_t cb_on_get, hook_t cb_on_del);
int atomic_hash_destroy (hmap_t *hmap);

int atomic_hash_add (hmap_t *hmap, const void *key, int key_len, void *user_data, int init_ttl, hook_t cb_on_dup, void *out);
int atomic_hash_del (hmap_t *hmap, const void *key, int key_len, hook_t cb_on_del, void *out); //delete all matches
int atomic_hash_get (hmap_t *hmap, const void *key, int key_len, hook_t cb_on_get, void *out); //get the first match

int atomic_hash_stats (hmap_t *hmap, unsigned long escaped_milliseconds);

#endif /* ATOMIC_HASH_H */
