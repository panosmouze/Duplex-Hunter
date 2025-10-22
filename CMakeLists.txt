cmake_minimum_required(VERSION 3.10)
project(DuplexHunter VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/external/json/single_include/)

file(GLOB_RECURSE SOURCES
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
)

add_executable(duplexhunter ${SOURCES})

find_library(XXHASH_LIB xxhash HINTS /usr/lib /usr/lib/x86_64-linux-gnu)
if(XXHASH_LIB)
    target_link_libraries(duplexhunter PRIVATE ${XXHASH_LIB})
else()
    message(FATAL_ERROR "xxHash library not found.")
endif()
