add_library(peyvand_util INTERFACE)

add_library(peyvand::util ALIAS peyvand_util)

target_include_directories(peyvand_util
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(peyvand_util INTERFACE unofficial::sqlite3::sqlite3)