add_library(yfitops_proto STATIC yfitops/messages.proto)
add_library(yfitops::proto ALIAS yfitops_proto)

target_link_libraries(yfitops_proto PUBLIC protobuf::libprotobuf)

set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
protobuf_generate(
  TARGET yfitops_proto
  IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
  PROTOC_OUT_DIR "${PROTO_GEN_DIR}")

target_include_directories(yfitops_proto PUBLIC "${PROTO_GEN_DIR}")
