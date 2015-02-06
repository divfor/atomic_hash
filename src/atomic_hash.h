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
 * Cooked from the CURL-project examples with thanks to the 
 * great CURL-project authors and contributors.
 */

#ifndef __ATOMIC_HASH_
#define __ATOMIC_HASH_
#include <stdint.h>

typedef int (*callback)(void *bucket_data, void *callback_args);

/* callback function idx */
#define DTOR_TRY_HIT 0
#define DTOR_TRY_ADD 1
#define DTOR_TRY_GET 2
#define DTOR_TRY_DEL 3
#define DTOR_EXPIRED 4
#define MAX_CALLBACK (DTOR_EXPIRED+1)

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
  volatile nid curr_blocks;
  void **ba;
  shared nid mask, shift;	/* used for i2p() only */
  shared nid max_blocks, blk_node_num, node_size;
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
  shared void (* hash_func) (const void *key, const size_t len, void *r);

/* destructor function, must non-block return as soon as possible !!! 
 * increase NHP if you cannot get them faster
 * return 0 to indicate removing the hash node
 * */
  shared callback dtor[MAX_CALLBACK];
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
atomic hash is a lock-free hash table designed for multiple threads to share cache or data with up to 2^32 items. It allows multiple threads to concurrent read/write/delete hash items without locks. 5M~20M ops/s can be performed in morden computer platform.
By giving max hash item number and expected collision rate, atomic_hash calculates two load factors and creates array 1 with higer load factor, array 2 with lower load factor, and a small arry 3 to store collision items. memory pool for hash nodes (not for user data) is also designed for both of high performance and memory saving. Both of successful and unsuccessful search from the hash table are O(1)

Usage
atomic_hash_add/get/del finds target bucket and holds on it for callback functions to read/copy/release user data or update ref counter. callback functions must be non-blocking to return as soon as possible, otherwise performance drops remarkablly. Define your callback funtions to access user data:
typedef int (*callback)(void *bucket_data, void *callback_args)
here 'bucket_data' is the input to callback function copied from 'hash_node->data' (generally a pointer to the user data structure), and 'callback_args' is output of callback function which may also take call-time input role together. It is the callback functions to take responsiblity of releasing user data. If it does it successfully, it should return 0 to tell atomic_hash to remove current hash node, otherwise, it should return non-zero. There are 5 callback functions you may want to define:
DTOR_TRY_HIT_func: atomic_hash_add will call it when the adding item's hash key exists, generally define NULL func for it if you do not want to do value/data copying or ref counter updating;
DTOR_TRY_ADD_func: atomic_hash_add will call it when adding new item, generally define NULL func for it;
DTOR_TRY_GET_func: atomic_hash_get will call it once find target. do value/data copy or updating data in it;
DTOR_TRY_DEL_func: atomic_hash_del will call it to release user data;
DTOR_EXPIRED_func: any of atomic_hash_add/get/del will call it when detecting an expired item. do remove/release user data in it;
For example,
callback dtor[] = {NULL, NULL, DTOR_TRY_GET_func, DTOR_TRY_DEL_func, NULL};
ph = atomic_hash_create (max_nodes_num, 0, dtor);

About TTL
TTL (in milliseconds) is designed to enable expire feature in hash table as a cache. Set 'lookup_reset_ttl' to 0 to disable this feature so that all hash items never expire. If lookup_reset_ttl is set to >0, you still can set 'initial_ttl' to 0 to mark hash items that never expire.
lookup_reset_ttl: each successful lookup by atomic_hash_add or atomic_hash_get will automatically reset target item's hash_node->expire to (now + lookup_reset_ttl).
initial_ttl：new item's hash_noe->expire is set to (now + initial_ttl) when adding to hash table. this item's hash_node->expire will NOT be reset to (now + lookup_reset_ttl) if initial_ttl == 0.
if a item's hash_node->expire == 0, atomic_hash will never call DTOR_EXPIRED callback function for this item. if hash_node->expire > 0, the item will expire when now > hash_node->expire, and after that time, this bucket item may be removed by any of hash add/get/del calls that traverses it (this also means, no active cleanup thread to clear expired item). So release your own data in your DTOR_EXPIRED callback function!!!
*/

/* return (int): 0 for successful operation and non-zero for unsuccessful operation */
hash_t * atomic_hash_create (size_t max_nodes, int lookup_reset_ttl, callback dtor[MAX_CALLBACK]);
int atomic_hash_destroy (hash_t *h);
int atomic_hash_add (hash_t *h, void *key, size_t key_len, void *data, int initial_ttl, void *dtor_arg); //add non-duplicate
int atomic_hash_del (hash_t *h, void *key, size_t key_len, void *dtor_arg); //delete all matches
int atomic_hash_get (hash_t *h, void *key, size_t key_len, void *dtor_arg); //get the first match
int atomic_hash_stats (hash_t *h, unsigned int escaped_milliseconds);
#endif
