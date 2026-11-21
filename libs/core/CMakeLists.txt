add_library(peyvand_core STATIC
    src/PeerID.cpp
    src/Migrations.cpp
    src/Dispatcher.cpp
)

add_library(peyvand::core ALIAS peyvand_core)

find_package(unofficial-sqlite3 CONFIG REQUIRED)

target_include_directories(peyvand_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(peyvand_core PUBLIC 
        unofficial::sqlite3::sqlite3 
        SQLiteCpp
        peyvand::proto
)

peyvand_set_warnings(peyvand_core)
