
# --  Vars  --
set(HMAP_SOURCES atomic_hash.c)
set(HMAP_INCLUDE_PUBLIC_DIRS ${atomic_hash_SOURCE_DIR}/include/)
set(HMAP_INCLUDE_PRIVATE_DIRS ${atomic_hash_SOURCE_DIR}/src/hash_functions/)
set(HMAP_COMPILE_OPTIONS -fPIC)
set(HMAP_LINK_LIBS m)


# --  CMake options  --
set(HASH_FUNCTION "CITY3HASH_128" CACHE STRING "Hash function to be used for hashmap (may depend on specific CPU arch or 3rd party libs)")
set_property(CACHE HASH_FUNCTION PROPERTY STRINGS "CITY3HASH_128;MURMUR3_128")   #"CITY3HASH_128;MPQ3HASH;NEWHASH;MURMUR3_128"


# IMPORTANT: Must match macros in `atomic_hash.h`
if (HASH_FUNCTION STREQUAL "CITY3HASH_128")
    list(APPEND HMAP_SOURCES
             hash_functions/hash_city.c)
    list(APPEND HMAP_COMPILE_OPTIONS
            -DHASH_FUNCTION=1
            -march=native -msse4.2)

#elseif (HASH_FUNCTION STREQUAL "MPQ3HASH")
#    list(APPEND HMAP_SOURCES
#             hash_functions/hash_mpq.c)
#    list(APPEND HMAP_COMPILE_OPTIONS
#            -DHASH_FUNCTION=3)
#
#elseif (HASH_FUNCTION STREQUAL "NEWHASH")
#    list(APPEND HMAP_SOURCES
#             hash_functions/hash_newhash.c)
#    list(APPEND HMAP_COMPILE_OPTIONS
#            -DHASH_FUNCTION=4)

elseif (HASH_FUNCTION STREQUAL "MURMUR3_128")
    list(APPEND HMAP_SOURCES
             hash_functions/hash_murmur3.c)
    list(APPEND HMAP_COMPILE_OPTIONS
            -DHASH_FUNCTION=5)

else()
    error("Invalid hash function")
endif()


# --  CMake targets  --
set(LIB_BASENAME "atomichash")

add_library(atomichash-object OBJECT ${HMAP_SOURCES})
target_include_directories(atomichash-object PRIVATE ${HMAP_INCLUDE_PRIVATE_DIRS} ${HMAP_INCLUDE_PUBLIC_DIRS})
target_include_directories(atomichash-object SYSTEM INTERFACE ${HMAP_INCLUDE_PUBLIC_DIRS})      # TODO: Revise (should be `include`able via <hashmap/header.h>)
target_compile_options(atomichash-object PRIVATE ${HMAP_COMPILE_OPTIONS})
target_link_libraries(atomichash-object PRIVATE ${HMAP_LINK_LIBS})

add_library(atomichash-shared SHARED ${HMAP_SOURCES})
target_include_directories(atomichash-shared PRIVATE ${HMAP_INCLUDE_PRIVATE_DIRS} ${HMAP_INCLUDE_PUBLIC_DIRS})
target_include_directories(atomichash-shared SYSTEM INTERFACE ${HMAP_INCLUDE_PUBLIC_DIRS})
target_compile_options(atomichash-shared PRIVATE ${HMAP_COMPILE_OPTIONS})
target_link_libraries(atomichash-shared PRIVATE ${HMAP_LINK_LIBS})
set_target_properties(atomichash-shared PROPERTIES OUTPUT_NAME ${LIB_BASENAME})

add_library(atomichash-static STATIC ${HMAP_SOURCES})
target_include_directories(atomichash-static PRIVATE ${HMAP_INCLUDE_PRIVATE_DIRS} ${HMAP_INCLUDE_PUBLIC_DIRS})
target_include_directories(atomichash-static SYSTEM INTERFACE ${HMAP_INCLUDE_PUBLIC_DIRS})
target_compile_options(atomichash-static PRIVATE ${HMAP_COMPILE_OPTIONS})
target_link_libraries(atomichash-static PRIVATE ${HMAP_LINK_LIBS})
set_target_properties(atomichash-static PROPERTIES OUTPUT_NAME ${LIB_BASENAME})


# --  Print info  --
message(STATUS "")
message(STATUS "Library base name     : ${LIB_BASENAME}")
message(STATUS "")
