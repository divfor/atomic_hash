# atomic_hash
a lock-free hash table designed for multiple threads to share a cache or data structure without lock API calls

read/write/delete conccurrent operations are allowed. 11M ops/s tested in dual Xeon E5-2650 CPU

both of successful and unsuccessful search from the hash table are O(1)

#Define your callback funtions to access hash data quickly

atomic_hash_add/get/del finds target bucket and holds on it for your callback functions to read/copy/release bucket data or update ref counter. your callback functions must non-block return as soon as possible, otherwise performance drops!!!

typedef int (*callback)(void *bucket_data, void *callback_args)

DTOR_TRY_HIT when adding but find target exists, return/memcpy data or update ref counter in this callback

DTOR_TRY_ADD whey adding and find an empty bucket, attach data in this callback

DTOR_TRY_GET when looking up and find the target bucket, return/memcpy data or update ref counter in this callback

DTOR_TRY_DEL when deleting and find target bucket, remove/release data in this callback

DTOR_EXPIRED when detecting an expired bucket, remove/release data in this callback

#About TTL (in ms)
if ttl = 0, bucket item will never expire and does not call DTOR_EXPIRED callback function. if ttl > 0, bucket item will expire by ttl and will be removed by any of hash_add/get/del calls if they see it is expired. So make sure your DTOR_EXPIRED callback function to release your own data!!!

lookup_reset_ttl: each time a successful lookup by hash_add or hash_get will automatically reset bucket item's expire timer to it.

initial_ttl：set the ttl when adding bucket item to hash table. will not be reset to lookup_reset_ttl if initial_ttl == 0.

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
