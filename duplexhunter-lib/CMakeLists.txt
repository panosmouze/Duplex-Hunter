file(GLOB_RECURSE LIB_SOURCES "${PROJECT_SOURCE_DIR}/duplexhunter-lib/src/*.cpp")
file(GLOB_RECURSE LIB_HEADERS "${PROJECT_SOURCE_DIR}/duplexhunter-lib/include/*.hpp")

add_library(duplexhunter SHARED ${LIB_SOURCES})

set_target_properties(duplexhunter PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN TRUE
    PUBLIC_HEADER "${LIB_HEADERS}"
)

install(TARGETS duplexhunter
    PUBLIC_HEADER DESTINATION include
    LIBRARY EXCLUDE_FROM_ALL
    ARCHIVE EXCLUDE_FROM_ALL
    RUNTIME EXCLUDE_FROM_ALL
)

target_include_directories(duplexhunter PUBLIC
    ${PROJECT_SOURCE_DIR}/duplexhunter-lib/include/
    ${PROJECT_SOURCE_DIR}/external/json/single_include/
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_link_libraries(duplexhunter PUBLIC ${XXHASH_LIB})