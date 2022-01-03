/*
 * atomic_hash.h
 *
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

/* callback function idx */
#define PLEASE_REMOVE_HASH_NODE    -1
#define PLEASE_SET_TTL_TO_DEFAULT  -2
#define PLEASE_DO_NOT_CHANGE_TTL   -3
#define PLEASE_SET_TTL_TO(n)       (n)


typedef struct hash_t hash_t;


// typedef int (*callback)(void *hash_data, void *caller_data);
typedef int (*hook) (void *hash_data, void *rtn_data);



/* -- Function prototypes -- */
/* For documentation, see README.md */
hash_t *atomic_hash_create (unsigned int max_nodes, int reset_ttl);

void atomic_hash_register_hooks(hash_t *h,
                                hook on_ttl, hook on_add, hook on_dup, hook on_get, hook on_del);

int atomic_hash_destroy (hash_t *h);
int atomic_hash_add (hash_t *h, const void *key, int key_len, void *user_data, int init_ttl, hook func_on_dup, void *out);
int atomic_hash_del (hash_t *h, const void *key, int key_len, hook func_on_del, void *out); //delete all matches
int atomic_hash_get (hash_t *h, const void *key, int key_len, hook func_on_get, void *out); //get the first match
int atomic_hash_stats (hash_t *h, unsigned long escaped_milliseconds);

#endif /* ATOMIC_HASH_H */
