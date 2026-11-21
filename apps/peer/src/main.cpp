#include "Peer.hpp"
#include <CLI/CLI.hpp>
#include <Migrations.hpp>
#include <peyvand/messages.pb.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <string>

#include <asio.hpp>

int main(int argc, char** argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    CLI::App app{"peyvand peer"};
    std::string data_dir_arg;
    uint16_t port = 9000;

    app.add_option("--data-dir", data_dir_arg, "Directory holding this peer's state")->required();
    app.add_option("--port", port, "UDP port to bind")->check(CLI::Range(1024, 65535));
    CLI11_PARSE(app, argc, argv);

    try {
        const auto data_dir = std::filesystem::absolute(data_dir_arg);
        std::filesystem::create_directories(data_dir);
        spdlog::info("state directory: {}", data_dir.string());

        SQLite::Database db((data_dir / "database.sqlite").string(),
                            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

        migrate(db); // schema guaranteed up to date

        asio::io_context ctx;
        Peer peer(ctx, port, db);

        asio::signal_set signals(ctx, SIGINT, SIGTERM);
        signals.async_wait([&](const asio::error_code& error, int signal_number) {
            if (!error) {
                spdlog::info("signal {} received, stopping", signal_number);
                ctx.stop();
            }
        });

        asio::co_spawn(ctx, peer.handleReq(), asio::detached);
        ctx.run();
    } catch (const std::exception& e) {
        spdlog::critical("fatal: {}", e.what());
        return 1;
    }
}