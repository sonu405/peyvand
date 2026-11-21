find_package(Threads REQUIRED)

# --- Asio (standalone) ---
find_package(asio CONFIG QUIET)           # vcpkg provides this
if(NOT TARGET asio::asio)                 # system package: header-only fallback
  find_path(ASIO_INCLUDE_DIR asio.hpp REQUIRED)
  add_library(asio::asio INTERFACE IMPORTED)
  target_include_directories(asio::asio INTERFACE "${ASIO_INCLUDE_DIR}")
  target_link_libraries(asio::asio INTERFACE Threads::Threads)
endif()
target_compile_definitions(asio::asio INTERFACE ASIO_STANDALONE)

# --- Protobuf ---
find_package(Protobuf CONFIG REQUIRED)    # gives protobuf::libprotobuf, protobuf::protoc

# --- SQLite ---
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# --- SQLiteCpp ---                       # wrapper for sqlite3
find_package(SQLiteCpp CONFIG REQUIRED)

# --- spdlog ---
find_package(spdlog CONFIG REQUIRED)      # gives spdlog::spdlog

# -- cli11 ---
find_package(CLI11 CONFIG REQUIRED)
