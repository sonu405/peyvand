add_library(peyvand_proto STATIC 
  peyvand/messages.proto
  peyvand/packet.proto
)
add_library(peyvand::proto ALIAS peyvand_proto)

target_link_libraries(peyvand_proto PUBLIC protobuf::libprotobuf)

set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
protobuf_generate(
  TARGET peyvand_proto
  IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
  PROTOC_OUT_DIR "${PROTO_GEN_DIR}")

target_include_directories(peyvand_proto PUBLIC "${PROTO_GEN_DIR}")
