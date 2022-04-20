
# -- Vars --
set(MAP_HEADERS
        atomic_hash.h)
set(MAP_SOURCES
        atomic_hash.c)

set(MAP_INCLUDE_DIRS "")
set(MAP_LIB_DIRS "")

set(MAP_COMPILE_OPTIONS
        -fPIC)

set(MAP_LINK_LIBS
        m)


# -- CMake options --
set(HASH_FUNCTION "CITY3HASH_128" CACHE STRING "Hash function to be used for hashmap (may depend on specific CPU arch or 3rd party libs)")
set_property(CACHE HASH_FUNCTION PROPERTY STRINGS "CITY3HASH_128;MD5HASH")


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
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPENSSL REQUIRED openssl)
    list(APPEND MAP_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIRS})
    list(APPEND MAP_LIB_DIRS ${OPENSSL_LIBRARY_DIRS})
    list(APPEND MAP_LINK_LIBS ${OPENSSL_LIBRARIES})

    list(APPEND MAP_HEADERS
        hash_functions/hash_md5.h)
    list(APPEND MAP_SOURCES
        hash_functions/hash_md5.c)
    list(APPEND MAP_COMPILE_OPTIONS
        -DHASH_FUNCTION=2)

else()
    error("Invalid hash function")
endif()


# -- "Project" --
add_library(atomic_hash_object OBJECT ${MAP_HEADERS} ${MAP_SOURCES})
target_compile_options(atomic_hash_object PRIVATE ${MAP_COMPILE_OPTIONS})
target_include_directories(atomic_hash_object PRIVATE ${MAP_INCLUDE_DIRS})
target_include_directories(atomic_hash_object SYSTEM INTERFACE ${CMAKE_CURRENT_LIST_DIR})      # TODO: Revise (should be `include`able via <hashmap/header.h>)
target_link_directories(atomic_hash_object PRIVATE ${MAP_LIB_DIRS})
target_link_libraries(atomic_hash_object PRIVATE ${MAP_LINK_LIBS})

add_library(atomic_hash SHARED ${MAP_HEADERS} ${MAP_SOURCES})
target_compile_options(atomic_hash PRIVATE ${MAP_COMPILE_OPTIONS})
target_include_directories(atomic_hash PRIVATE ${MAP_INCLUDE_DIRS})
target_link_directories(atomic_hash PRIVATE ${MAP_LIB_DIRS})
target_link_libraries(atomic_hash PRIVATE ${MAP_LINK_LIBS})

add_library(atomic_hash_static STATIC ${MAP_HEADERS} ${MAP_SOURCES})
target_compile_options(atomic_hash_static PRIVATE ${MAP_COMPILE_OPTIONS})
target_include_directories(atomic_hash_static PRIVATE ${MAP_INCLUDE_DIRS})
target_link_directories(atomic_hash_static PRIVATE ${MAP_LIB_DIRS})
target_link_libraries(atomic_hash_static PRIVATE ${MAP_LINK_LIBS})
