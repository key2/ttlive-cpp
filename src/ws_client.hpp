#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ttlive {

/// Secure WebSocket client on libcurl-impersonate (Chrome fingerprint).
///
/// Used for TikTok's webcast push socket
/// (wss://webcast-ws.<region>.tiktok.com/webcast/im/ws_proxy/...). The same
/// Chrome TLS/HTTP fingerprint that gets the REST calls past the WAF is needed
/// for the WS upgrade, so this reuses curl-impersonate rather than Beast.
///
/// Single-threaded usage: connect(), then send_binary()/recv_binary() from the
/// same thread. curl WS handles are not thread-safe.
class WsClient {
public:
    WsClient();
    ~WsClient();

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    /// Establish the WSS connection. Throws std::runtime_error on failure
    /// (including a non-101 handshake, which signals TikTok rejection).
    void connect(const std::string& wss_url,
                 const std::string& cookie_header,
                 const std::string& user_agent);

    /// Send a binary frame. Returns false on error (connection lost).
    bool send_binary(const std::vector<uint8_t>& data);

    /// Result of a recv attempt.
    enum class RecvStatus { Message, Again, Closed, Error };

    /// Receive one complete binary message into ``out`` (reassembling fragmented
    /// frames). Returns:
    ///   Message — ``out`` holds a full binary payload
    ///   Again   — no data available right now (retry after a short sleep)
    ///   Closed  — server closed the connection
    ///   Error   — transport error
    RecvStatus recv_binary(std::vector<uint8_t>& out);

    void close();
    bool connected() const { return connected_.load(); }

private:
    struct Impl;
    Impl* impl_;
    std::atomic<bool> connected_{false};
};

}  // namespace ttlive
