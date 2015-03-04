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


#About TTL
TTL (in milliseconds) is designed to enable timer for hash nodes. Set 'reset_ttl' to 0 to disable this feature so that all hash items never expire. If reset_ttl is set to >0, you still can set 'init_ttl' to 0 to mark specified hash items that never expire.

reset_ttl: atomic_hash_create uses it to set hash_node->expire. each successful lookup by atomic_hash_add or atomic_hash_get may reset target hash node's hash_node->expire to (now + reset_ttl), per your on_dup / on_get hook functions;

init_ttl：atomic_hash_add uses it to set hash_node->expire to (now + init_ttl). If init_ttl == 0, hash_node will never expires as it will NOT be reset by reset_ttl.

hash_node->expire: hash node's 'expire' field. If expire == 0, this hash node will never expire; If expire > 0, this hash node will become expired when current time is larger than expire, but no removal action immediately applies on it. However, since it's expired, it may be removed by any of hash add/get/del calls that traverses it (in another words, no active cleanup thread to clear expired item). So your must free user data's memory in your own hash_handle->on_ttl hook function!!!


#Installation

Step 1, build dynamic shared libatomic_hash.so: 

cd src && make clean && make


Step 2, copy libatomic_hash.so to /usr/lib64/ and atomic_hash.h to /usr/include/

make install


Step 3, include "atomic_hash.h" in your source file(s) and dynamic link atomic_hash lib to your program. see test/makefile example
