#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "ttlive/events.hpp"

namespace ttlive {

/// Options controlling how the client connects.
struct ClientOptions {
    /// Use the real-time WebSocket transport (low latency). Falls back to HTTP
    /// long-polling automatically if the WS handshake fails.
    bool use_websocket = true;

    /// Whether to check the user is live before connecting (recommended).
    bool fetch_live_check = true;

    /// Whether to fetch room info (stream FLV/HLS/RTMP URLs) on connect.
    bool fetch_stream_info = true;

    /// Whether to fetch the room's gift list (id/name/diamond value) on connect.
    /// Off by default: the response is large (~2 MB / hundreds of gifts).
    bool fetch_gift_list = false;

    /// Whether to emit the events in the very first fetch response (backlog of
    /// recent comments etc.) as normal events.
    bool process_connect_events = true;

    /// Override the room id and skip HTML/API scraping entirely.
    int64_t room_id_override = 0;

    /// Optional cookies to seed the jar (e.g. a real session's ttwid /
    /// msToken / sessionid). A valid ttwid is currently required for TikTok to
    /// return live messages. Format: "name1=val1; name2=val2".
    std::string cookies;

    /// Directory containing the TikTok SDK JS files used by the QuickJS signer.
    /// Empty = the compile-time default (the project's ``js/`` directory).
    std::string js_dir;

    /// Optional debug sink receiving 100% of the payload bytes received from
    /// TikTok, *before* any parsing/filtering, so nothing can be silently
    /// dropped. Called from the client thread with:
    ///   kind = "ws_frame"      raw incoming WebSocket binary frame
    ///                          (WebcastPushFrame, as received)
    ///   kind = "im_fetch"      raw /webcast/im/fetch/ HTTP response body
    ///                          (ProtoMessageFetchResult)
    ///   kind = "msg:<Method>"  each decoded webcast message's payload
    ///                          (e.g. "msg:WebcastGiftMessage")
    std::function<void(const std::string& kind, const uint8_t* data, size_t len)>
        raw_sink;
};

using EventCallback = std::function<void(const Event&)>;

/// A client that connects to a single TikTok LIVE room and streams events.
///
/// Usage:
///   ttlive::TikTokLiveClient client("msk.0011");
///   client.on(ttlive::EventType::Comment, [](const ttlive::Event& e){ ... });
///   client.run();   // blocks until the stream ends / disconnect()
class TikTokLiveClient {
public:
    explicit TikTokLiveClient(std::string unique_id, ClientOptions options = {});
    ~TikTokLiveClient();

    TikTokLiveClient(const TikTokLiveClient&) = delete;
    TikTokLiveClient& operator=(const TikTokLiveClient&) = delete;

    /// Register a callback for a specific event type. Multiple callbacks per
    /// type are supported and invoked in registration order.
    void on(EventType type, EventCallback cb);

    /// Register a callback that receives *every* event regardless of type.
    void on_any(EventCallback cb);

    /// Connect and block the calling thread, pumping events into callbacks
    /// until the stream ends or ``disconnect()`` is called. Throws
    /// std::runtime_error on connection failure (user offline, blocked, etc.).
    void run();

    /// Request a graceful disconnect from another thread.
    void disconnect();

    /// The resolved room id (0 until connected).
    int64_t room_id() const;

    /// The cleaned unique id.
    const std::string& unique_id() const;

    /// Live stream playback URLs (FLV/HLS/RTMP by quality). Populated on
    /// connect; empty if fetching room info failed or the stream is offline.
    const StreamInfo& stream_info() const;

    /// The room's gift list. Populated on connect only when
    /// ClientOptions::fetch_gift_list is true; otherwise empty. Can also be
    /// fetched on demand after connecting via fetch_gift_list().
    const std::vector<GiftInfo>& gift_list() const;

    /// Fetch (or re-fetch) the room's gift list on demand. Requires the client
    /// to have resolved a room id (i.e. after run() has connected, or with a
    /// room_id_override). Returns the parsed list and caches it.
    const std::vector<GiftInfo>& fetch_gift_list();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ttlive
