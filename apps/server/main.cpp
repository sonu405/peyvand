#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <yfitops/messages.pb.h>

#include <asio.hpp>

int main() {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  asio::io_context io;
  yfitops::ChatMessage msg;
  msg.set_body("hello");

  spdlog::info("sqlite {}, msg bytes={}, io stopped={}", sqlite3_libversion(),
               msg.ByteSizeLong(), io.stopped());
}
