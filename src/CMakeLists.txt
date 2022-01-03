
# -- Variables --
set(MAP_HEADERS
        atomic_hash.h)
set(MAP_SOURCES
        atomic_hash.c)

set(MAP_COMPILE_OPTIONS
        -fPIC)

set(MAP_LINK_LIBS
        m)


# -- CMake options --
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Default build type: Release" FORCE)
endif()

set(HASH_FUNCTION "CITY3HASH_128" CACHE STRING "Hash function to be used for hashmap (may depend on specific CPU arch or 3rd party libs)")
set(HASH_FUNCTION_VALUES "CITY3HASH_128;MD5HASH")
set_property(CACHE HASH_FUNCTION PROPERTY STRINGS ${HASH_FUNCTION_VALUES})


# IMPORTANT: Must match macros in `atomic_hash.h`
if (HASH_FUNCTION STREQUAL "CITY3HASH_128")
    list(APPEND MAP_COMPILE_OPTIONS
        -DHASH_FUNCTION=1
        -march=native -msse4.2)
    list(APPEND MAP_HEADERS
        hash_functions/hash_city.h)
    list(APPEND MAP_SOURCES
        hash_functions/hash_city.c)

elseif (HASH_FUNCTION STREQUAL "MD5HASH")
    list(APPEND MAP_HEADERS
        hash_functions/hash_md5.h)
    list(APPEND MAP_SOURCES
        hash_functions/hash_md5.c)
    list(APPEND MAP_COMPILE_OPTIONS
        -DHASH_FUNCTION=2)
    list(APPEND MAP_LINK_LIBS
        ssl
        crypto)

else()
    error("Invalid hash function")
endif()


# -- "Project" --
add_library(atomic_hash_object OBJECT
        ${MAP_HEADERS}
        ${MAP_SOURCES})
target_compile_options(atomic_hash_object PUBLIC ${MAP_COMPILE_OPTIONS})
target_link_libraries(atomic_hash_object PRIVATE ${MAP_LINK_LIBS})
target_include_directories(atomic_hash_object SYSTEM INTERFACE ${CMAKE_CURRENT_LIST_DIR})      # TODO: Revise (should be `include`able via <hashmap/header.h>)

add_library(atomic_hash SHARED
        ${MAP_HEADERS}
        ${MAP_SOURCES})
target_compile_options(atomic_hash PUBLIC ${MAP_COMPILE_OPTIONS})
target_link_libraries(atomic_hash PRIVATE ${MAP_LINK_LIBS})

add_library(atomic_hash_static STATIC
        ${MAP_HEADERS}
        ${MAP_SOURCES})
target_compile_options(atomic_hash_static PUBLIC ${MAP_COMPILE_OPTIONS})
target_link_libraries(atomic_hash_static PRIVATE ${MAP_LINK_LIBS})
