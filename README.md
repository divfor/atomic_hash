# atomic_hash
a lock-free hash table designed for multiple threads to share a cache or data structure without lock API calls

read/write/delete conccurrent operations are allowed. 11M ops/s tested in dual Xeon E5-2650 CPU

both of successful and unsuccessful search from the hash table are O(1)

