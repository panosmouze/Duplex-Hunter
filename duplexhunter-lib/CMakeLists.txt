file(GLOB_RECURSE LIB_SOURCES "*.cpp")

add_library(duplexhunter SHARED ${LIB_SOURCES})

set_target_properties(duplexhunter PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

target_include_directories(duplexhunter PUBLIC
    ${PROJECT_SOURCE_DIR}/duplexhunter-lib/include/
    ${PROJECT_SOURCE_DIR}/external/json/single_include/
)

target_link_libraries(duplexhunter PUBLIC ${XXHASH_LIB})