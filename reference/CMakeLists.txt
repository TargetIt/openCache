cmake_minimum_required(VERSION 3.14)
project(gpgpu_cache_ref VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Reference GPGPU-Sim cache library
# gpu-cache.h and gpu-cache.cc are UNMODIFIED from GPGPU-Sim.
# Only #include directives were changed to use gpgpu_stubs.h.
add_library(gpgpu_cache_ref STATIC
    gpgpu_cache/gpu_cache_ref.cc
)

target_include_directories(gpgpu_cache_ref PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/gpgpu_cache
)

# Tests
add_executable(test_gpgpu_cache_ref
    test/test_main.cc
)
target_link_libraries(test_gpgpu_cache_ref gpgpu_cache_ref)

# Compiler options
if(MSVC)
    target_compile_options(gpgpu_cache_ref PRIVATE /W3 /EHsc)
else()
    target_compile_options(gpgpu_cache_ref PRIVATE -Wall -Wextra -O2 -g)
endif()

enable_testing()
add_test(NAME gpgpu_cache_ref_tests COMMAND test_gpgpu_cache_ref)
