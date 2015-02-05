# atomic_hash
a lock-free hash table designed for multiple threads to share a cache or data structure without lock API calls

read/write/delete conccurrent operations are allowed. 11M ops/s tested in dual Xeon E5-2650 CPU

both of successful and unsuccessful search from the hash table are O(1)

Install Steps:
Step 1, build dynamic shared libatomic_hash.so:

cd src && make clean && make


Step 2, copy libatomic_hash.so to /usr/lib64/ and atomic_hash.h to /usr/include/

make install


Step 3, include "atomic_hash.h" in your source file(s) and dynamic link atomic_hash lib to your program
