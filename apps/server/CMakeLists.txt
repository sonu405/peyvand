find_package(fmt CONFIG REQUIRED)

add_executable(yfitops_server
    main.cpp
)

find_package(unofficial-sqlite3 CONFIG REQUIRED)

target_link_libraries(yfitops_server
    PRIVATE
        yfitops::core
        yfitops::proto
        fmt::fmt
        unofficial::sqlite3::sqlite3
)

yfitops_set_warnings(yfitops_server)



