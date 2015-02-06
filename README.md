# Summary
atomic hash is a lock-free hash table designed for multiple threads to share cache or data with up to 2^32 items. It allows multiple threads to concurrent read/write/delete hash items without locks. 5M~20M ops/s can be performed in morden computer platform.

By giving max hash item number and expected collision rate, atomic_hash calculates two load factors and creates array 1 with higer load factor, array 2 with lower load factor, and a small arry 3 to store collision items. memory pool for hash nodes (not for user data) is also designed for both of high performance and memory saving. Both of successful and unsuccessful search from the hash table are O(1)

# Usage
atomic_hash_add/get/del finds target bucket and holds on it for callback functions to read/copy/release user data or update ref counter. callback functions must be non-blocking to return as soon as possible, otherwise performance drops remarkablly. Define your callback funtions to access user data: 

typedef int (*callback)(void *bucket_data, void *callback_args)

here 'bucket_data' is the input to callback function copied from 'hash_node->data' (generally a pointer to the user data structure), and 'callback_args' is output of callback function which may also take call-time input role together. It is the callback functions to take responsiblity of releasing user data. If it does it successfully, it should return 0 to tell atomic_hash to remove current hash node, otherwise, it should return non-zero. There are 5 callback functions you may want to define:

DTOR_TRY_HIT_func: atomic_hash_add will call it when find the adding hash key exists, generally define NULL func for it if you do not want to do value/data copying or ref counter updating;

DTOR_TRY_ADD_func: atomic_hash_add will call it when link new hash node to an empty bucket, generally define NULL func for it;

DTOR_TRY_GET_func: atomic_hash_get will call it when find a target. do value/data copy or updating data in it;

DTOR_TRY_DEL_func: atomic_hash_del call it to release user data and then remove current hash node;

DTOR_EXPIRED when detecting an expired bucket, remove/release data in this callback

Example:

callback dtor[] = {DTOR_TRY_HIT_func, DTOR_TRY_ADD_func, DTOR_TRY_GET_func, DTOR_TRY_DEL_func, DTOR_EXPIRED_func};

ph = atomic_hash_create (num_strings, TTL_ON_AUTO_RESET, dtor);

#About TTL (in ms) of Bucket Item
if ttl == 0, bucket item will never expire and does not call DTOR_EXPIRED callback function. if ttl > 0, bucket item will timeout after ttl, and this bucket item may be removed by any of hash add/get/del calls that sees it. So release your own data in your DTOR_EXPIRED callback function!!!

lookup_reset_ttl: each successful lookup by atomic_hash_add or atomic_hash_get will automatically reset bucket item's expire timer to (now + lookup_reset_ttl).

initial_ttl：set bucket's expire time as now + ttl when adding bucket item to hash table. bucket's expire time will NOT be reset with lookup_reset_ttl if initial_ttl == 0.

#Lib Functions

hash_t * atomic_hash_create (size_t max_nodes, int lookup_reset_ttl, callback dtor[MAX_CALLBACK])

int atomic_hash_destroy (hash_t *h)

int atomic_hash_add (hash_t *h, void *key, size_t key_len, void *data, int initial_ttl, void *dtor_arg)

int atomic_hash_del (hash_t *h, void *key, size_t key_len, void *dtor_arg)

int atomic_hash_get (hash_t *h, void *key, size_t key_len, void *dtor_arg)

int atomic_hash_stats (hash_t *h, unsigned int escaped_milliseconds)


#Installation

Step 1, build dynamic shared libatomic_hash.so: 

cd src && make clean && make


Step 2, copy libatomic_hash.so to /usr/lib64/ and atomic_hash.h to /usr/include/

make install


Step 3, include "atomic_hash.h" in your source file(s) and dynamic link atomic_hash lib to your program. see test/makefile example
