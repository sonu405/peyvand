add_library(yfitops_core STATIC
    src/core.cpp
)

add_library(yfitops::core ALIAS yfitops_core)

target_include_directories(yfitops_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

yfitops_set_warnings(yfitops_core)
