find_package(fmt CONFIG REQUIRED)

add_executable(peyvand_peer
    src/main.cpp
    src/peer.cpp
)

target_include_directories(peyvand_peer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(peyvand_peer
    PRIVATE
        peyvand::proto
        peyvand::core
        fmt::fmt
        unofficial::sqlite3::sqlite3
        asio::asio
        SQLiteCpp
        spdlog::spdlog
        CLI11::CLI11
)

peyvand_set_warnings(peyvand_peer)



