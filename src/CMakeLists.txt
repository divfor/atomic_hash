
# -- Variables --
set(MAP_SOURCE
        atomic_hash.h
        atomic_hash.c)

set(MAP_COMPILE_OPTIONS
        -fPIC)

set(MAP_LINK_LIBS
        m)


# -- CMake options --
set(FUNCTION "CITY3HASH_128" CACHE STRING "Function to be used")
set(FUNCTION_VALUES "CITY3HASH_128;MD5HASH")
set_property(CACHE FUNCTION PROPERTY STRINGS ${FUNCTION_VALUES})


# IMPORTANT: Must match macros in `atomic_hash.h`
if (FUNCTION STREQUAL "CITY3HASH_128")
    list(APPEND MAP_COMPILE_OPTIONS
        -DFUNCTION=1
        -march=native -msse4.2)
    list(APPEND MAP_SOURCE
        hash_functions/hash_city.h
        hash_functions/hash_city.c)
elseif (FUNCTION STREQUAL "MD5HASH")
    list(APPEND MAP_SOURCE
        hash_functions/hash_md5.h
        hash_functions/hash_md5.c)
    list(APPEND MAP_COMPILE_OPTIONS
        -DFUNCTION=2)
    list(APPEND MAP_LINK_LIBS
        ssl
        crypto)
else()
    error("Invalid function")
endif()


# -- "Project" --
add_library(atomic_hash_object OBJECT ${MAP_SOURCE})
target_compile_options(atomic_hash_object PUBLIC ${MAP_COMPILE_OPTIONS})
target_link_libraries(atomic_hash_object PRIVATE ${MAP_LINK_LIBS})
target_include_directories(atomic_hash_object SYSTEM INTERFACE ${CMAKE_CURRENT_LIST_DIR})      # TODO: Revise (should be `include`able via <hashmap/header.h>)

add_library(atomic_hash SHARED  ${MAP_SOURCE})
target_compile_options(atomic_hash PUBLIC ${MAP_COMPILE_OPTIONS})
target_link_libraries(atomic_hash PRIVATE ${MAP_LINK_LIBS})

add_library(atomic_hash_static STATIC  ${MAP_SOURCE})
target_compile_options(atomic_hash_static PUBLIC ${MAP_COMPILE_OPTIONS})
target_link_libraries(atomic_hash_static PRIVATE ${MAP_LINK_LIBS})
