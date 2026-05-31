cmake_minimum_required(VERSION 3.14)
project(gem5_cache_ref VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GEM5_STUBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gem5_stubs)
set(GEM5_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Stub implementation library
add_library(gem5_stubs STATIC
    gem5_stubs/stubs.cc
)
target_include_directories(gem5_stubs PUBLIC
    ${GEM5_STUBS_DIR}
    ${GEM5_SRC_DIR}
)

# Reference gem5 cache library (UNMODIFIED source)
add_library(gem5_cache_ref STATIC
    src/mem/cache/cache_blk.cc
    src/mem/cache/mshr.cc
    src/mem/cache/mshr_queue.cc
    src/mem/cache/write_queue.cc
    src/mem/cache/write_queue_entry.cc
    src/mem/cache/tags/base.cc
    src/mem/cache/tags/base_set_assoc.cc
    src/mem/cache/tags/fa_lru.cc
    src/mem/cache/tags/indexing_policies/set_associative.cc
    src/mem/cache/replacement_policies/lru_rp.cc
    src/mem/cache/replacement_policies/fifo_rp.cc
    src/mem/cache/replacement_policies/mru_rp.cc
    src/mem/cache/replacement_policies/random_rp.cc
    src/mem/cache/replacement_policies/tree_plru_rp.cc
)
target_link_libraries(gem5_cache_ref gem5_stubs)

# Compiler options
if(MSVC)
    target_compile_options(gem5_cache_ref PRIVATE /W3 /EHsc)
else()
    target_compile_options(gem5_cache_ref PRIVATE
        -Wall -Wextra
        -Wno-non-pod-varargs
        -Wno-unused-parameter
        -Wno-unused-variable
        -O2 -g
    )
endif()

# Unit tests
add_executable(test_gem5_cache_ref
    test/test_main.cc
)
target_link_libraries(test_gem5_cache_ref gem5_cache_ref)

enable_testing()
add_test(NAME gem5_cache_ref_tests COMMAND test_gem5_cache_ref)
