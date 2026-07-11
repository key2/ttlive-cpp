#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "ttlive/client.hpp"

namespace {
ttlive::TikTokLiveClient* g_client = nullptr;

void on_sigint(int) {
    if (g_client) g_client->disconnect();
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "Usage: %s <@username | username> [options]\n"
                     "Options:\n"
                     "  --room-id <id>     Skip room scraping, connect directly\n"
                     "  --no-live-check    Skip the is-live check\n"
                     "  --cookies \"k=v;..\" Seed cookies (e.g. ttwid/msToken/sessionid)\n"
                     "  --js-dir <path>    Directory with TikTok SDK JS files\n"
                     "  --gifts            Fetch and print the room gift list on connect\n"
                     "  --no-ws            Use HTTP long-polling instead of WebSocket\n"
                     "Example: %s @msk.0011\n",
                     argv[0], argv[0]);
        return 2;
    }

    std::string username = argv[1];
    ttlive::ClientOptions opts;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--room-id" && i + 1 < argc) {
            opts.room_id_override = std::stoll(argv[++i]);
        } else if (a == "--no-live-check") {
            opts.fetch_live_check = false;
        } else if (a == "--cookies" && i + 1 < argc) {
            opts.cookies = argv[++i];
        } else if (a == "--js-dir" && i + 1 < argc) {
            opts.js_dir = argv[++i];
        } else if (a == "--gifts") {
            opts.fetch_gift_list = true;
        } else if (a == "--no-ws") {
            opts.use_websocket = false;
        }
    }

    ttlive::TikTokLiveClient client(username, opts);
    g_client = &client;
    std::signal(SIGINT, on_sigint);

    client.on(ttlive::EventType::Connect, [](const ttlive::Event& e) {
        std::cout << "[CONNECTED] @" << e.unique_id << " room_id=" << e.room_id << "\n";
        if (!e.stream.qualities.empty() || !e.stream.rtmp_pull_url.empty()) {
            std::cout << "[STREAM] default=" << e.stream.default_quality << "\n";
            for (const auto& q : e.stream.qualities) {
                std::cout << "  " << q.quality << " (" << q.label << "): "
                          << (q.flv_url.empty() ? q.hls_url : q.flv_url) << "\n";
            }
            if (!e.stream.rtmp_pull_url.empty())
                std::cout << "  RTMP: " << e.stream.rtmp_pull_url << "\n";
        }
    });

    client.on_any([&client](const ttlive::Event& e) {
        // Print the gift-list summary once, right after Connect.
        static bool printed = false;
        if (e.type == ttlive::EventType::Connect && !printed) {
            printed = true;
            const auto& gl = client.gift_list();
            if (!gl.empty()) {
                std::cout << "[GIFTS] " << gl.size() << " gifts available; e.g.:\n";
                for (size_t i = 0; i < gl.size() && i < 8; ++i)
                    std::cout << "  #" << gl[i].id << " " << gl[i].name
                              << " (" << gl[i].diamond_count << " diamonds)\n";
            }
        }
    });

    client.on(ttlive::EventType::Comment, [](const ttlive::Event& e) {
        std::cout << "[CHAT] " << e.user.nickname << " (@" << e.user.unique_id
                  << "): " << e.comment << "\n";
    });

    client.on(ttlive::EventType::Gift, [](const ttlive::Event& e) {
        std::cout << "[GIFT] " << e.user.nickname << " sent gift '" << e.gift_name
                  << "' (id=" << e.gift_id << ") x" << e.repeat_count
                  << (e.gift_streaking ? " [streaking]" : "") << "\n";
    });

    client.on(ttlive::EventType::Like, [](const ttlive::Event& e) {
        std::cout << "[LIKE] " << e.user.nickname << " +" << e.like_count
                  << " (total " << e.total_likes << ")\n";
    });

    client.on(ttlive::EventType::Join, [](const ttlive::Event& e) {
        std::cout << "[JOIN] " << e.user.nickname << " joined\n";
    });

    client.on(ttlive::EventType::Follow, [](const ttlive::Event& e) {
        std::cout << "[FOLLOW] " << e.user.nickname << " followed\n";
    });

    client.on(ttlive::EventType::Share, [](const ttlive::Event& e) {
        std::cout << "[SHARE] " << e.user.nickname << " shared the stream\n";
    });

    client.on(ttlive::EventType::Subscribe, [](const ttlive::Event& e) {
        std::cout << "[SUBSCRIBE] " << e.user.nickname << " subscribed\n";
    });

    client.on(ttlive::EventType::RoomUserSeq, [](const ttlive::Event& e) {
        std::cout << "[VIEWERS] " << e.viewer_count << " watching\n";
    });

    client.on(ttlive::EventType::LiveEnd, [](const ttlive::Event&) {
        std::cout << "[LIVE ENDED]\n";
    });

    client.on(ttlive::EventType::Disconnect, [](const ttlive::Event&) {
        std::cout << "[DISCONNECTED]\n";
    });

    try {
        client.run();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
