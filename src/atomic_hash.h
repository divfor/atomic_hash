/* 
 *  atomic_hash.h
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

#ifndef __ATOMIC_HASH_
#define __ATOMIC_HASH_
#include <stdint.h>

typedef int (*callback)(void *hash_data, void *caller_data);
typedef int (* hook) (void *hash_data, void *rtn_data);

/* callback function idx */
#define PLEASE_REMOVE_HASH_NODE    -1
#define PLEASE_SET_TTL_TO_DEFAULT  -2
#define PLEASE_DO_NOT_CHANGE_TTL   -3
#define PLEASE_SET_TTL_TO(n)       (n)

//#define MD5HASH
//#define MURMUR3HASH_128
#define CITY3HASH_128
//#define NEWHASH
//#define MPQ3HASH

#if defined(MPQ3HASH) || defined (NEWHASH)
#define NCMP 3
typedef uint32_t hvu;
typedef struct hv { hvu x, y, z; } hv_t;
#elif defined (CITY3HASH_128) || defined (MURMUR3HASH_128) || defined (MD5HASH)
#define NCMP 2
typedef uint64_t hvu;
typedef struct hv { hvu x, y; } hv;
#endif

#define shared  __attribute__((aligned(64)))

typedef uint32_t nid;
typedef struct hstats
{
  unsigned long expires;
  unsigned long escapes;
  unsigned long add_nomem;
  unsigned long add_nosit;
  unsigned long del_nohit;
  unsigned long get_nohit;
  unsigned long mem_htabs;
  unsigned long mem_nodes;
  unsigned long max_nodes;
  unsigned long key_collided;
} hstats_t;

typedef struct hash_counters
{
  unsigned long xadd, xget, xdel, nexp;
} hc_t; /* 64-bytes cache line */

typedef struct mem_pool
{
  void **ba;
  shared nid mask, shift;	/* used for i2p() only */
  shared nid max_blocks, blk_node_num, node_size, blk_size;
  volatile nid curr_blocks;
} mem_pool_t;

typedef union {
  struct { nid mi, rfn; };
  uint64_t all;
} cas_t;

typedef struct hash_node
{
  volatile hv v;
  unsigned long expire; /* expire in ms # of gettimeofday(), 0 = never */
  void *data;
} node_t;

typedef struct htab
{
  nid *b;             /* hash tab (int arrary as memory index */
  unsigned long ncur, n, nb;  /* nb: buckets #, set by n * r */
  unsigned long nadd, ndup, nget, ndel;
} htab_t;

typedef struct hash
{
/* hash function, here select cityhash_128 as default */
  shared void (* hash_func) (const void *key, size_t len, void *r);

/* hook func to deal with user data in safe zone */
  shared hook on_ttl, on_add, on_dup, on_get, on_del;
  shared volatile cas_t freelist; /* free hash node list */
  shared htab_t ht[3]; /* ht[2] for array [MINTAB] */
  shared hstats_t stats;
  shared void **hp;
  shared mem_pool_t *mp;
  shared unsigned long reset_expire; /* if > 0, reset node->expire */
  shared unsigned long nmht, ncmp;
  shared unsigned long nkey, npos, nseat; /* nseat = 2*npos = 4*nkey */
  shared void *teststr;
  shared unsigned long testidx, teststr_num;
} hash_t;


/*
Summary
This is a hash table designed with high performance, lock-free and memory-saving. Multiple threads can concurrently perform read/write/delete operations up to 10M ops/s in mordern computer platform. It supports up to 2^32 hash items with O(1) performance for both of successful and unsuccessful search from the hash table.
By giving max hash item number, atomic_hash calculates two load factors to match expected collision rate and creates array 1 with higer load factor, array 2 with lower load factor, and a small arry 3 to store collision items. memory pool for hash nodes (not for user data) is also designed for both of high performance and memory saving.

Usage
There are three atomic hash functions (atomic_hash_add/get/del). Generally they find target hash node, hold on it safely for a while to call hook function to read/copy/update/release user data:
typedef int (*hook)(void *hash_data, void *return_data)
here 'hash_data' will be copied from 'hash_node->data' (generally a pointer to the user data structure), and 'return_data' will be given by caller. The hook function must be non-blocking and spends time as less as possible, otherwise performance will drop remarkablly. The hook function should take care user data's memory if it returns -1(PLEASE_REMOVE_HASH_NODE), or simply returns either -2(PLEASE_SET_TTL_TO_DEFAULT) or a positive ttl number to indicate updating this node's expiration timer. actions for other return values are not defined. hook functions can be registered with your own hook functions after hash table is created, to replace the default ones that do not free any memory:
  h->on_ttl = default_func_remove_node;    -- return PLEASE_REMOVE_HASH_NODE
  h->on_del = default_func_remove_node;    -- return PLEASE_REMOVE_HASH_NODE
  h->on_add = default_func_not_change_ttl; -- return PLEASE_DO_NOT_CHANGE_TTL
  h->on_get = default_func_not_change_ttl; -- return PLEASE_DO_NOT_CHANGE_TTL
  h->on_dup = default_func_reset_ttl;      -- return PLEASE_SET_TTL_TO_DEFAULT
For more flexibility, below hash functions can use different hook functions in call-time:
atomic_hash_add(new_on_dup), atomic_hash_get(new_on_get), atomic_hash_del(new_on_del)

About TTL
TTL (in milliseconds) is designed to enable timer for hash nodes. Set 'reset_ttl' to 0 to disable this feature so that all hash items never expire. If reset_ttl is set to >0, you still can set 'init_ttl' to 0 to mark specified hash items that never expire.
reset_ttl: atomic_hash_create uses it to set hash_node->expire. each successful lookup by atomic_hash_add or atomic_hash_get may reset target item's hash_node->expire to (now + reset_ttl), per your on_dup / on_get hook functions;
init_ttl：atomic_hash_add uses it to set hash_node->expire to (now + init_ttl). If init_ttl == 0, hash_node will never expires as it will NOT be reset by reset_ttl.
hash_node->expire: hash node's 'expire' field. If expire == 0, this hash node will never expire; If expire > 0, this hash node will become expired when current time is larger than expire, but no removal action immediately applies on it. However, since it's expired, it may be removed by any of hash add/get/del calls that traverses it (in another words, no active cleanup thread to clear expired item). So your must free user data's memory in your own hash_handle->on_ttl hook function!!!
*/

/* return (int): 0 for successful operation and non-zero for unsuccessful operation */
hash_t * atomic_hash_create (unsigned int max_nodes, int reset_ttl);
int atomic_hash_destroy (hash_t *h);
int atomic_hash_add (hash_t *h, void *key, int key_len, void *user_data, int init_ttl, hook func_on_dup, void *out);
int atomic_hash_del (hash_t *h, void *key, int key_len, hook func_on_del, void *out); //delete all matches
int atomic_hash_get (hash_t *h, void *key, int key_len, hook func_on_get, void *out); //get the first match
int atomic_hash_stats (hash_t *h, unsigned long escaped_milliseconds);
#endif
