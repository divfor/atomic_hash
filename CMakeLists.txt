cmake_minimum_required(VERSION 3.9.4)

project(atomic_hash C)


# -- Project settings --
set(CMAKE_C_STANDARD 99)
#set(CMAKE_C_COMPILER clang)

# - CMake Debugging options -
#set(CMAKE_VERBOSE_MAKEFILE ON)
#set(CMAKE_DEBUG_TARGET_PROPERTIES INCLUDE_DIRECTORIES)


# -- Preprocessor / Compile options --
add_compile_options(
    -Wall -Wextra -pedantic
    -Wformat=2
    -Wdouble-promotion
    -Wfloat-equal
    -Winit-self
    -Wundef -Wunused-macros
    -Wswitch-enum -Wswitch-default
    $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
#    -Werror -pedantic-errors
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
            -Walloc-zero
            -Wformat-signedness
            -Wduplicated-cond
            -frecord-gcc-switches
#            -march=native -mtune=native                  # ALWAYS optimize 4 local machine  (since built binaries won't be distributed anyways); NOTE: Doesn't work currently w/ clang on Apple Silicone  (SEE: https://discourse.llvm.org/t/why-does-march-native-not-work-on-apple-m1/2733)
    )
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED)
if (LTO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)         # Enable LTO by default (I.E., 4 all targets (SEE: https://stackoverflow.com/a/47370726))
else()
    message(STATUS "LTO isn't supported")
endif()


# -- CMake options --
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    message(STATUS "No build type selected, defaulting to ** ${CMAKE_BUILD_TYPE} **")
endif()


# -- "Subdirs" containing projects --
add_subdirectory(src/)
add_subdirectory(test/)


# --  Print info  --
message(STATUS "")
message(STATUS "Build type            : ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler exec       : ${CMAKE_C_COMPILER}")
message(STATUS "")
