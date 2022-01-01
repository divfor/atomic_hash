
set(CMAKE_BUILD_TYPE Debug)     # Ensure debug info and assert functions are build during compile of tests


# -- CMake options --
option(WITH_BENCHMARK_TEST "Build benchmark test (works only on GNU/Linux ??)" OFF)


if (WITH_BENCHMARK_TEST)
    # -- Project(s) --
    add_executable(mthread_test
            utils/mtrand.c
            utils/mtrand.h
            mthread_test.c)
    target_link_libraries(mthread_test
            atomic_hash_object
            pthread)
    target_include_directories(mthread_test PRIVATE
            $<TARGET_PROPERTY:atomic_hash_object,INTERFACE_INCLUDE_DIRECTORIES>)

    set(TEST_DEMO_DATA_DIR "test/test-files")
    set(TEST_DEMO_DATA_SRC_DIR "${CMAKE_SOURCE_DIR}/${TEST_DEMO_DATA_DIR}")
    set(TEST_DEMO_DATA_DEST_DIR "${CMAKE_BINARY_DIR}/${TEST_DEMO_DATA_DIR}")
    set(TEST_DEMO_DATA_TAR "top-1m.csv.tar.gz")
    add_custom_command(
            TARGET mthread_test POST_BUILD
            COMMENT "Copy demo test file and extract it"

            COMMAND ${CMAKE_COMMAND} -E copy_directory ${TEST_DEMO_DATA_SRC_DIR} ${TEST_DEMO_DATA_DEST_DIR}
            COMMAND ${CMAKE_COMMAND} -E chdir ${TEST_DEMO_DATA_DEST_DIR} tar xzf ${TEST_DEMO_DATA_TAR}
            COMMAND ${CMAKE_COMMAND} -E remove ${TEST_DEMO_DATA_DEST_DIR}/${TEST_DEMO_DATA_TAR})
endif()
