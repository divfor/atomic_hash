# Summary
This is a hash table designed with high performance, lock-free and memory-saving. Multiple threads can concurrently perform read/write/delete operations up to 10M ops/s in mordern computer platform. It supports up to 2^32 hash items with O(1) performance for both of successful and unsuccessful search from the hash table.

By giving max hash item number, atomic_hash calculates two load factors to match expected collision rate and creates array 1 with higer load factor, array 2 with lower load factor, and a small arry 3 to store collision items. memory pool for hash nodes (not for user data) is also designed for both of high performance and memory saving.

# Usage
Use below functions to create a hash handle that assosiates its arrays and memory pool, print statistics of it, or release it.
```c
hash_t * atomic_hash_create (unsigned int max_nodes, int reset_ttl);
int atomic_hash_stats (hash_t *h, unsigned long escaped_milliseconds);
int atomic_hash_destroy (hash_t *h);
```
The hash handle can be copied to any number of threads for calling below hash functions: 
```c
int atomic_hash_add (hash_t *h, void *key, int key_len, void *user_data, int init_ttl, hook func_on_dup, void *out);
int atomic_hash_del (hash_t *h, void *key, int key_len, hook func_on_del, void *out); //delete all matches
int atomic_hash_get (hash_t *h, void *key, int key_len, hook func_on_get, void *out); //get the first match
```
Not like normal hash functions that return user data directly, atomic hash functions return status code -- 0 for successful operation and non-zero for unsuccessful operation. Instead, atomic hash functions call hook functions to deal with user data once they find a target hash node. the hook functions should be defined as following format:
```c
typedef int (*hook)(void *hash_data, void *out)
```
here 'hash_data' will be copied from target hash node's 'data' field by atomic hash functions (generally it is a pointer to link the user data), and 'out' will be given by atomic hash function's caller. The hook function should obey below rules:
```c
1. must be non-blocking and spends time as less as possible, otherwise performance will drop remarkablly;
2. move out or free user data's memory if it returns -1(PLEASE_REMOVE_HASH_NODE) to indicate deleting this hash node;
3. returns -2 (PLEASE_SET_TTL_TO_DEFAULT) or a positive ttl number if caller wants to indicate hash function to update this hash node's expiration timer;
```
atomic_hash_create will register below built-in hook functions as default to hash_handle function pointers on_ttl/on_del/on_add/on_get/on_dup. The built-in hook functions only do value-copy of hash node's 'data' field and then return code:
```c
  h->on_ttl = default_func_remove_node;    //must return PLEASE_REMOVE_HASH_NODE
  h->on_del = default_func_remove_node;    //must return PLEASE_REMOVE_HASH_NODE
  h->on_add = default_func_not_change_ttl; //must return PLEASE_DO_NOT_CHANGE_TTL (ttl given by initial_ttl)
  h->on_get = default_func_not_change_ttl; //may return PLEASE_DO_NOT_CHANGE_TTL or a positive number
  h->on_dup = default_func_reset_ttl;      //may return PLEASE_SET_TTL_TO_DEFAULT or a positive number
  ```
So you need to write your own hook functions with above return code requirements to replace default ones if you want to free/move away your user data's memeory or adjust ttl in the fly.
```c
h->on_ttl = your_own_remove_node;
h->on_add = your_own_on_add_hook_fun
```
At last, as listed above, hash functions atomic_hash_add, atomic_hash_get, atomic_hash_del are able to replace hook function in call-time. This will increase flexibility to deal with different user data type in a same hash table.


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
