cmake_minimum_required(VERSION 3.14)
project(openCache VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===== Core Library =====
set(OPENCACHE_SOURCES
    src/cache_config.cc
    src/tag_array.cc
    src/open_cache.cc
)

set(OPENCACHE_HEADERS
    src/open_cache_types.h
    src/cache_config.h
    src/cache_block.h
    src/tag_array.h
    src/mshr.h
    src/cache_stats.h
    src/open_cache.h
)

add_library(openCache STATIC ${OPENCACHE_SOURCES} ${OPENCACHE_HEADERS})
target_include_directories(openCache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ===== Scenario Library =====
set(SCENARIO_SOURCES
    scenario/scenarios.cc
)
set(SCENARIO_HEADERS
    scenario/scenarios.h
)

add_library(openCacheScenarios STATIC ${SCENARIO_SOURCES} ${SCENARIO_HEADERS})
target_include_directories(openCacheScenarios PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/scenario)
target_link_libraries(openCacheScenarios openCache)

# ===== Tests =====
add_executable(test_openCache
    test/test_main.cc
)
target_link_libraries(test_openCache openCache openCacheScenarios)

# ===== Enable testing (CTest) =====
enable_testing()
add_test(NAME openCache_unit_tests COMMAND test_openCache)

# ===== Compiler options =====
if(MSVC)
    target_compile_options(openCache PRIVATE /W3 /EHsc)
else()
    target_compile_options(openCache PRIVATE -Wall -Wextra -O2 -g)
endif()

# ===== Install =====
install(TARGETS openCache openCacheScenarios
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib)
install(FILES ${OPENCACHE_HEADERS} DESTINATION include/openCache)
