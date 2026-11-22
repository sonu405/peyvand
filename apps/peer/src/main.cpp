#include "Peer.hpp"
#include <CLI/CLI.hpp>
#include <Migrations.hpp>
#include <peyvand/messages.pb.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <string>

#include <asio.hpp>

#include <spdlog/cfg/env.h>

int main(int argc, char** argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    // spdlog::cfg::load_env_levels(); // then run: SPDLOG_LEVEL=debug ./peyvand_peer ...
    // spdlog::set_level(spdlog::level::debug);

    CLI::App app{"peyvand peer"};
    std::string data_dir_arg;
    uint16_t port = 9000;

    std::string bootstrap_file_path;

    app.add_option("--data-dir", data_dir_arg, "Directory holding this peer's state")->required();
    app.add_option("--port", port, "UDP port to bind")->check(CLI::Range(1024, 65535));
    app.add_option("--bootstrap_file", bootstrap_file_path,
                   "File listing bootstrap peers, one ip:port per line")
        ->check(CLI::ExistingFile);
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

        peer.loadKnownPeers();
        //  ---- Bootstrapping candidates ----
        if (!bootstrap_file_path.empty()) {
            for (const auto& ep : peer.load_bootstrap_file(bootstrap_file_path))
                peer.peerBook.addCandidate(ep);
        }

        asio::signal_set signals(ctx, SIGINT, SIGTERM);
        signals.async_wait([&](const asio::error_code& error, int signal_number) {
            if (error)
                return;
            spdlog::info("signal {} received, stopping", signal_number);
            peer.GoodbyeAll(); // synchronous sends, before anything is torn down
            peer.saveKnownPeers();

            ctx.stop();
        });

        asio::co_spawn(ctx, peer.handleReq(), asio::detached);
        asio::co_spawn(ctx, peer.probeLoop(), asio::detached);
        asio::co_spawn(ctx, peer.heartBeatsLoop(), asio::detached);
        ctx.run();
    } catch (const std::exception& e) {
        spdlog::critical("fatal: {}", e.what());
        return 1;
    }
}