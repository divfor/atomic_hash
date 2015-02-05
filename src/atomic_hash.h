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
/* atomic_hash_add/get/del finds target bucket and holds on it for your
 * callback functions to read/copy/release bucket data or update ref counter
 * DO NOT spend much time in your callback functions, otherwise performance drops!!!
 */
typedef int (*callback)(void *bucket_data, void *callback_args);

/* destructor callback function idx */
#define DTOR_TRY_HIT 0 /* when adding but find target exists, return/memcpy data or update ref counter here */
#define DTOR_TRY_ADD 1 /* whey adding and find an empty bucket, attach data here */
#define DTOR_TRY_GET 2 /* when looking up and find the target bucket, return/memcpy data or update ref counter here */
#define DTOR_TRY_DEL 3 /* when deleting and find target bucket, remove/release data here */
#define DTOR_EXPIRED 4 /* when detecting an expired bucket, remove/release data here */
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

hash_t * atomic_hash_create (size_t max_nodes, unsigned long lookup_reset_ttl, callback dtor[MAX_CALLBACK]);
int atomic_hash_destroy (hash_t *h);
int atomic_hash_add (hash_t *h, void *key, size_t key_len, void *data, int initial_ttl, void *dtor_arg);
int atomic_hash_del (hash_t *h, void *key, size_t key_len, void *dtor_arg);
int atomic_hash_get (hash_t *h, void *key, size_t key_len, void *dtor_arg);
int atomic_hash_stats (hash_t *h, unsigned int escaped_milliseconds);
#endif
