# Summary
atomic hash is a lock-free hash table designed for multiple threads to share cache or data with up to 2^32 items. It allows multiple threads to concurrent read/write/delete hash items without locks. 5M~20M ops/s can be performed in morden computer platform.

By giving max hash item number and expected collision rate, atomic_hash calculates two load factors and creates array 1 with higer load factor, array 2 with lower load factor, and a small arry 3 to store collision items. memory pool for hash nodes (not for user data) is also designed for both of high performance and memory saving. Both of successful and unsuccessful search from the hash table are O(1)

#Hash Functions
hash_t * atomic_hash_create (size_t max_nodes, int lookup_reset_ttl, callback dtor[MAX_CALLBACK])

int atomic_hash_destroy (hash_t *h)

int atomic_hash_add (hash_t *h, void *key, size_t key_len, void *data, int initial_ttl, void *callback_arg) // add non-dup

int atomic_hash_del (hash_t *h, void *key, size_t key_len, void *callback_arg) // del all matches

int atomic_hash_get (hash_t *h, void *key, size_t key_len, void *callback_arg) // get first match

int atomic_hash_stats (hash_t *h, unsigned int escaped_milliseconds)

return (int): 0 for successful operation and non-zero for unsuccessful operation

# Usage
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

#About TTL
TTL (in milliseconds) is designed to enable expire feature in hash table as a cache. Set 'lookup_reset_ttl' to 0 to disable this feature so that all hash items never expire. If lookup_reset_ttl is set to >0, you still can set 'initial_ttl' to 0 to mark hash items that never expire.

lookup_reset_ttl: each successful lookup by atomic_hash_add or atomic_hash_get will automatically reset target item's hash_node->expire to (now + lookup_reset_ttl).

initial_ttl：new item's hash_noe->expire is set to (now + initial_ttl) when adding to hash table. this item's hash_node->expire will NOT be reset to (now + lookup_reset_ttl) if initial_ttl == 0.

if a item's hash_node->expire == 0, atomic_hash will never call DTOR_EXPIRED callback function for this item. if hash_node->expire > 0, the item will expire when now > hash_node->expire, and after that time, this bucket item may be removed by any of hash add/get/del calls that traverses it (this also means, no active cleanup thread to clear expired item). So release your own data in your DTOR_EXPIRED callback function!!!

#Installation

Step 1, build dynamic shared libatomic_hash.so: 

cd src && make clean && make


Step 2, copy libatomic_hash.so to /usr/lib64/ and atomic_hash.h to /usr/include/

make install


Step 3, include "atomic_hash.h" in your source file(s) and dynamic link atomic_hash lib to your program. see test/makefile example
