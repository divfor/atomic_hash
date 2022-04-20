# atomic_hash

## Summary
This is a hash table designed with high performance, lock-free and memory-saving. Multiple threads can concurrently perform read/write/delete operations up to 10M ops/s in mordern computer platform. It supports up to 2^32 hash items with O(1) performance for both of successful and unsuccessful search from the hash table.

By giving max hash item number, atomic_hash calculates two load factors to match expected collision rate and creates array 1 with higer load factor, array 2 with lower load factor, and a small arry 3 to store collision items. memory pool for hash nodes (not for user data) is also designed for both of high performance and memory saving.

A design description (in chinese) is posted here:
https://blog.csdn.net/divfor/article/details/44316291

## Usage
Use below functions to create a hash handle that associates its arrays and memory pool, print statistics of it, or release it.
```c
hash_t * atomic_hash_create (unsigned int max_nodes, int reset_ttl);
int atomic_hash_stats (hash_t *h, unsigned long escaped_milliseconds);
int atomic_hash_destroy (hash_t *h);
```
The hash handle can be copied to any number of threads for calling below hash functions:
```c
int atomic_hash_add (hash_t *h, void *key, int key_len, void *user_data, int init_ttl, hook_t func_on_dup, void *out);
int atomic_hash_del (hash_t *h, void *key, int key_len, hook_t func_on_del, void *out); //delete all matches
int atomic_hash_get (hash_t *h, void *key, int key_len, hook_t func_on_get, void *out); //get the first match
```
Not like normal hash functions that return user data directly, atomic hash functions return status code -- 0 for successful operation and non-zero for unsuccessful operation. Instead, atomic hash functions call hook functions to deal with user data once they find target hash node. The hook functions should be defined as following format:
```c
typedef int (*hook_t)(void *hash_data, void *out)
```
here `hash_data` will be copied from target hash node's `data` field by atomic hash functions (generally it is a pointer to link the user data), and `out` will be given by atomic hash function's caller. There are 5 function pointers (`cb_on_ttl`, `cb_on_del`, `cb_on_add`, `cb_on_get` and `cb_on_dup`) to register hook functions. The hook function should obey below rules:
  1. must be non-blocking and essential actions only. too much execution time will drop performance remarkablly;
  2. `cb_on_ttl` and `cb_on_del` should free user data and must return -1 (`HOOK_REMOVE_HASH_NODE`).
  3. `cb_on_get` and `cb_on_dup` may return either -2 (`HOOK_SET_TTL_TO_DEFAULT`) or a positive number that indicates updating ttl;
  4. `cb_on_add` must return -3 (`HOOK_DONT_CHANGE_TTL`) as ttl will be set by `intital_ttl`;

`atomic_hash_create` will initialize some built-in functions as default hook functions that only do value-copy for hash node's 'data' field and then return code. So you need to write your own hook functions to replace default ones if you want to free your user data's memeory or adjust ttl in the fly:
  ```c
  h->cb_on_ttl = your_own_on_ttl_hook_func;
  h->cb_on_add = your_own_on_add_hook_func;
  ...
  ```
In the call time, instead of hook functions registered in `cb_on_dup`/`cb_on_get`/`cb_on_del`, hash functions `atomic_hash_add`, `atomic_hash_get`, `atomic_hash_del` are able to use an alertative function as long as they obey above hook function rules. This will give flexibility to deal with different user data type in a same hash table.

### About TTL
TTL (in milliseconds) is designed to enable timer for hash nodes. Set `reset_ttl` to 0 to disable this feature so that all hash items never expire. If `reset_ttl` is set to >0, you still can set `init_ttl` to 0 to mark specified hash items that never expire.

`reset_ttl`: `atomic_hash_create` uses it to set `hash_node->expire`. each successful lookup by `atomic_hash_add` or `atomic_hash_get` may reset target hash node's `hash_node->expire` to (now + `reset_ttl`), per your `cb_on_dup` / `cb_on_get` hook functions;

`init_ttl`：`atomic_hash_add` uses it to set `hash_node->expire` to (now + `init_ttl`). If `init_ttl` == 0, hash_node will never expires as it will NOT be reset by `reset_ttl`.

`hash_node->expire`: hash node's 'expire' field. If `expire` == 0, this hash node will never expire; If `expire` > 0, this hash node will become expired when current time is larger than expire, but no removal action immediately applies on it. However, since it's expired, it may be removed by any of hash add/get/del calls that traverses it (in another words, no active cleanup thread to clear expired item). So your must free user data's memory in your own `hash_handle->cb_on_ttl` hook function!!!


## Build
### Prerequisites
* Installed pkg-config, cmake & ccmake (Note: ccmake is optional):
    * Debian/Ubuntu: `sudo apt install -y pkg-config cmake cmake-curses-gui`
    * macOS: `brew install pkg-config cmake`
* Requirements based on chosen cmake options:
  * `HASH_FUNCTION`: `MD5HASH` requires OpenSSL (package `libssl-dev` for Debian/Ubuntu)

### Out-of-source build
1. `mkdir build && cd build`
2. `ccmake -DCMAKE_BUILD_TYPE=Release ..` &rarr; press `c` &rarr; press `c` &rarr; press `g`
3. `cmake --build .`
    * Library (static & shared) will be in `build/src`
