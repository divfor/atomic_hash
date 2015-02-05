# atomic_hash
a lock-free hash table designed for multiple threads to share a cache or data structure without lock API calls

read/write/delete conccurrent operations are allowed. 11M ops/s tested in dual Xeon E5-2650 CPU

both of successful and unsuccessful search from the hash table are O(1)

#Define your callback funtions to access hash data quickly
atomic_hash_add/get/del finds target bucket and holds on it for your callback functions to read/copy/release bucket data or update ref counter. DO NOT spend much time in your callback functions, otherwise performance drops!!!

typedef int (*callback)(void *bucket_data, void *callback_args)

DTOR_TRY_HIT when adding but find target exists, return/memcpy data or update ref counter in this callback

DTOR_TRY_ADD whey adding and find an empty bucket, attach data in this callback

DTOR_TRY_GET when looking up and find the target bucket, return/memcpy data or update ref counter in this callback

DTOR_TRY_DEL when deleting and find target bucket, remove/release data in this callback

DTOR_EXPIRED when detecting an expired bucket, remove/release data in this callback

#About TTL (in ms)
if set to 0, bucket item will never expire; if set to >0, bucket item will expire by ttl and will be removed by any of hash_add/get/del calls if they see it is expired. So define your DTOR_EXPIRED callback to release your own data!

lookup_reset_ttl to auto reset bucket's ttl each time there is a successful lookup in hash_add or hash_get

initial_ttl only set the ttl

hash_t * atomic_hash_create (size_t max_nodes, unsigned long lookup_reset_ttl, callback dtor[MAX_CALLBACK])

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
