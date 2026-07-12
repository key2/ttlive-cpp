#include "ttlive/client.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <zlib.h>

#include "event_parser.hpp"
#include "http_client.hpp"
#include "qjs_signer.hpp"
#include "tiktok.pb.h"
#include "web_defaults.hpp"
#include "ws_client.hpp"

namespace ttlive {

const char* to_string(EventType type) {
    switch (type) {
        case EventType::Connect: return "Connect";
        case EventType::Disconnect: return "Disconnect";
        case EventType::Comment: return "Comment";
        case EventType::Gift: return "Gift";
        case EventType::Like: return "Like";
        case EventType::Join: return "Join";
        case EventType::Follow: return "Follow";
        case EventType::Share: return "Share";
        case EventType::Subscribe: return "Subscribe";
        case EventType::RoomUserSeq: return "RoomUserSeq";
        case EventType::Control: return "Control";
        case EventType::LiveEnd: return "LiveEnd";
        case EventType::Unknown: return "Unknown";
    }
    return "Unknown";
}

namespace {

std::string clean_unique_id(std::string id) {
    // Strip URL prefixes / @ / /live suffix.
    const std::string app = web_defaults::kAppUrl;
    auto pos = id.find(app + "/");
    if (pos != std::string::npos) id.erase(pos, app.size() + 1);
    // remove "/live"
    auto live = id.find("/live");
    if (live != std::string::npos) id.erase(live, 5);
    if (!id.empty() && id.front() == '@') id.erase(id.begin());
    // trim whitespace
    while (!id.empty() && std::isspace((unsigned char)id.front())) id.erase(id.begin());
    while (!id.empty() && std::isspace((unsigned char)id.back())) id.pop_back();
    return id;
}

std::string bytes_to_string(const std::vector<uint8_t>& p) {
    return std::string(reinterpret_cast<const char*>(p.data()), p.size());
}
std::vector<uint8_t> string_to_bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// gzip-decompress (auto-detect header). Returns input on failure.
std::string gunzip(const std::string& in) {
    if (in.empty()) return in;
    z_stream zs{};
    if (inflateInit2(&zs, 15 + 32) != Z_OK) return in;
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());
    std::string out;
    char buf[16384];
    int ret;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&zs);
            return in;
        }
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (ret != Z_STREAM_END && zs.avail_in > 0);
    inflateEnd(&zs);
    return out;
}

// Unescape a JSON string body (handles \/, \uXXXX for ASCII, \n, \t, \", \\).
std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'u' && i + 5 < s.size()) {
                int code = std::stoi(s.substr(i + 2, 4), nullptr, 16);
                if (code < 0x80) { out += static_cast<char>(code); i += 5; continue; }
                // non-ASCII: keep as-is (URLs are ASCII anyway)
                i += 5; continue;
            }
            switch (n) {
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                default: out += n; break;
            }
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Extract the raw substring value of a JSON string field: "key":"<value>".
// Returns the unescaped value, or "" if not found.
std::string json_string_field(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    std::string raw;
    for (size_t i = p; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) { raw += json[i]; raw += json[i + 1]; ++i; continue; }
        if (json[i] == '"') break;
        raw += json[i];
    }
    return json_unescape(raw);
}

// Extract a JSON integer field: "key":<digits> (returns 0 if absent).
int64_t json_int_field(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t p = json.find(needle);
    if (p == std::string::npos) return 0;
    size_t i = p + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '"')) ++i;
    bool neg = false;
    if (i < json.size() && json[i] == '-') { neg = true; ++i; }
    int64_t v = 0;
    bool any = false;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        v = v * 10 + (json[i] - '0'); ++i; any = true;
    }
    if (!any) return 0;
    return neg ? -v : v;
}

// Extract the raw substring of a JSON object value: "key":{ ... } (balanced).
std::string json_object_field(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":{";
    size_t p = json.find(needle);
    if (p == std::string::npos) return {};
    size_t start = p + needle.size() - 1;  // at the '{'
    int depth = 0;
    bool in_str = false;
    for (size_t i = start; i < json.size(); ++i) {
        char c = json[i];
        if (in_str) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '{') ++depth;
        else if (c == '}') { if (--depth == 0) return json.substr(start, i - start + 1); }
    }
    return {};
}

}  // namespace

struct TikTokLiveClient::Impl {
    std::string unique_id;
    ClientOptions options;
    int64_t room_id = 0;
    StreamInfo stream_info;
    std::vector<GiftInfo> gift_list;

    HttpClient http;
    std::unique_ptr<QuickJsSigner> signer;
    WsClient ws;
    int64_t hb_seq = 1;

    std::map<EventType, std::vector<EventCallback>> handlers;
    std::vector<EventCallback> any_handlers;

    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};

    void emit(const Event& ev) {
        for (const auto& cb : any_handlers) cb(ev);
        auto it = handlers.find(ev.type);
        if (it != handlers.end())
            for (const auto& cb : it->second) cb(ev);
    }

    // Forward raw received bytes to the debug sink (if installed).
    void sink(const std::string& kind, const void* data, size_t len) {
        if (options.raw_sink)
            options.raw_sink(kind, static_cast<const uint8_t*>(data), len);
    }

    // ---- REST steps -------------------------------------------------------

    std::string signed_url(const std::string& base_url_no_query,
                           const ParamList& params) {
        std::string base_query = web_defaults::encode_query(params);
        // The QuickJS signer runs TikTok's real SDK, which reads document.cookie
        // to include a fresh msToken and appends X-Bogus / X-Gnarly.
        signer->set_cookies(http.cookie_header());
        return signer->sign(base_url_no_query + "?" + base_query);
    }

    // Extract the first "roomId":"<digits>" occurrence via a plain scan.
    // std::regex is avoided here because the profile HTML is ~250KB and a
    // backtracking non-greedy match over it is slow / stack-hungry.
    static int64_t scan_room_id(const std::string& body) {
        const std::string key = "\"roomId\":";
        size_t pos = body.find(key);
        while (pos != std::string::npos) {
            size_t i = pos + key.size();
            // skip optional quote/space
            while (i < body.size() && (body[i] == '"' || body[i] == ' ')) ++i;
            size_t start = i;
            while (i < body.size() && body[i] >= '0' && body[i] <= '9') ++i;
            if (i > start) {
                int64_t rid = std::stoll(body.substr(start, i - start));
                if (rid > 0) return rid;
            }
            pos = body.find(key, pos + key.size());
        }
        return 0;
    }

    int64_t fetch_room_id_from_html() {
        std::string url = std::string(web_defaults::kAppUrl) + "/@" + unique_id + "/live";
        HttpResponse resp = http.get(url);
        // Detect the anti-bot WAF interstitial ("Please wait..." challenge).
        if (resp.body.find("_wafchallengeid") != std::string::npos ||
            resp.body.find("wafchallenge") != std::string::npos) {
            throw std::runtime_error(
                "TikTok WAF challenge received (anti-bot). The client needs a "
                "valid ttwid/msToken cookie bootstrap or a challenge solver.");
        }
        int64_t rid = scan_room_id(resp.body);
        if (rid == 0 && resp.body.find("\"status\":4") != std::string::npos) {
            throw std::runtime_error("User is offline");
        }
        return rid;
    }

    int64_t fetch_room_id_from_api() {
        ParamList params = web_defaults::base_web_params();
        params.push_back({"uniqueId", unique_id});
        params.push_back({"sourceType", "54"});
        std::string url = std::string(web_defaults::kAppUrl) + "/api-live/user/room/?" +
                          web_defaults::encode_query(params);
        HttpResponse resp = http.get(url);
        if (resp.body.find("user_not_found") != std::string::npos) {
            throw std::runtime_error("User not found: " + unique_id);
        }
        return scan_room_id(resp.body);
    }

    bool fetch_is_live() {
        ParamList params = web_defaults::base_web_params();
        params.push_back({"room_ids", std::to_string(room_id)});
        std::string url = std::string(web_defaults::kWebcastUrl) + "/room/check_alive/?" +
                          web_defaults::encode_query(params);
        HttpResponse resp = http.get(url);
        // {"data":[{"alive":true,...}]}
        return resp.body.find("\"alive\":true") != std::string::npos;
    }

    // Fetch /webcast/room/info/ and extract the stream playback URLs (FLV/HLS
    // by quality + RTMP). Returns an empty StreamInfo on failure.
    StreamInfo fetch_room_info() {
        StreamInfo info;
        ParamList params = web_defaults::base_web_params();
        params.push_back({"room_id", std::to_string(room_id)});
        std::string url = std::string(web_defaults::kWebcastUrl) + "/room/info/?" +
                          web_defaults::encode_query(params);
        HttpResponse resp = http.get(
            url, {{"Referer", "https://www.tiktok.com/@" + unique_id + "/live"}});
        if (resp.status != 200 || resp.body.empty()) return info;

        const std::string& body = resp.body;

        // The stream URLs live under data.stream_url. Narrow to that object.
        std::string stream_url = json_object_field(body, "stream_url");
        const std::string& scope = stream_url.empty() ? body : stream_url;

        info.default_quality = json_string_field(scope, "default_resolution");

        // resolution_name maps quality key -> human label.
        std::string res_names = json_object_field(scope, "resolution_name");

        // flv_pull_url: { "HD1": "url", "SD1": "url", ... }
        std::string flv_obj = json_object_field(scope, "flv_pull_url");
        std::string hls_obj = json_object_field(scope, "hls_pull_url_map");
        // hls_pull_url_map entries are objects; skip structured HLS for now and
        // just take the flat hls_pull_url if present.
        std::string flat_hls = json_string_field(scope, "hls_pull_url");

        // Parse the flv object's "KEY":"URL" pairs.
        auto parse_pairs = [](const std::string& obj,
                              std::vector<std::pair<std::string, std::string>>& out) {
            size_t i = 0;
            while (i < obj.size()) {
                size_t k0 = obj.find('"', i);
                if (k0 == std::string::npos) break;
                size_t k1 = obj.find('"', k0 + 1);
                if (k1 == std::string::npos) break;
                std::string key = obj.substr(k0 + 1, k1 - k0 - 1);
                size_t colon = obj.find(':', k1);
                if (colon == std::string::npos) break;
                // value must be a string
                size_t v0 = obj.find('"', colon);
                if (v0 == std::string::npos) break;
                std::string raw;
                size_t j = v0 + 1;
                for (; j < obj.size(); ++j) {
                    if (obj[j] == '\\' && j + 1 < obj.size()) { raw += obj[j]; raw += obj[j + 1]; ++j; continue; }
                    if (obj[j] == '"') break;
                    raw += obj[j];
                }
                out.push_back({key, json_unescape(raw)});
                i = j + 1;
            }
        };

        std::vector<std::pair<std::string, std::string>> flv_pairs, hls_pairs;
        if (!flv_obj.empty()) parse_pairs(flv_obj, flv_pairs);

        for (const auto& kv : flv_pairs) {
            StreamQuality q;
            q.quality = kv.first;
            q.flv_url = kv.second;
            q.label = json_string_field(res_names, kv.first);
            info.qualities.push_back(std::move(q));
        }

        info.rtmp_pull_url = json_string_field(scope, "rtmp_pull_url");
        if (info.qualities.size() == 1 && !flat_hls.empty())
            info.qualities.front().hls_url = flat_hls;

        return info;
    }

    // Fetch /webcast/gift/list/ and extract each gift's id/name/diamonds/icon.
    std::vector<GiftInfo> fetch_gift_list_impl() {
        std::vector<GiftInfo> gifts;
        ParamList params = web_defaults::base_web_params();
        params.push_back({"room_id", std::to_string(room_id)});
        std::string url = std::string(web_defaults::kWebcastUrl) + "/gift/list/?" +
                          web_defaults::encode_query(params);
        HttpResponse resp = http.get(
            url, {{"Referer", "https://www.tiktok.com/@" + unique_id + "/live"}});
        if (resp.status != 200 || resp.body.empty()) return gifts;

        const std::string& body = resp.body;
        // Locate the "gifts":[ ... ] array.
        size_t arr = body.find("\"gifts\":[");
        if (arr == std::string::npos) return gifts;
        size_t i = arr + 9;  // just after '['

        // Walk top-level {} objects inside the array until the matching ']'.
        while (i < body.size()) {
            // skip whitespace/commas
            while (i < body.size() && (body[i] == ' ' || body[i] == ',' || body[i] == '\n')) ++i;
            if (i >= body.size() || body[i] == ']') break;
            if (body[i] != '{') { ++i; continue; }

            // find the balanced end of this object
            size_t start = i;
            int depth = 0;
            bool in_str = false;
            for (; i < body.size(); ++i) {
                char c = body[i];
                if (in_str) {
                    if (c == '\\') { ++i; continue; }
                    if (c == '"') in_str = false;
                    continue;
                }
                if (c == '"') in_str = true;
                else if (c == '{') ++depth;
                else if (c == '}') { if (--depth == 0) { ++i; break; } }
            }
            std::string obj = body.substr(start, i - start);

            GiftInfo g;
            g.id = json_int_field(obj, "id");
            g.name = json_string_field(obj, "name");
            g.diamond_count = static_cast<int32_t>(json_int_field(obj, "diamond_count"));
            g.describe = json_string_field(obj, "describe");
            g.type = static_cast<int32_t>(json_int_field(obj, "type"));
            // icon.url_list[0] — grab the first URL after the "icon" object.
            std::string icon_obj = json_object_field(obj, "icon");
            if (!icon_obj.empty()) {
                size_t ul = icon_obj.find("\"url_list\":[");
                if (ul != std::string::npos) {
                    size_t q0 = icon_obj.find('"', ul + 11);
                    if (q0 != std::string::npos) {
                        size_t q1 = icon_obj.find('"', q0 + 1);
                        if (q1 != std::string::npos)
                            g.icon_url = json_unescape(icon_obj.substr(q0 + 1, q1 - q0 - 1));
                    }
                }
            }
            if (g.id != 0) gifts.push_back(std::move(g));
        }
        return gifts;
    }

    // Fetch one ProtoMessageFetchResult from TikTok's /webcast/im/fetch/
    // endpoint (signed via QuickJS). On TikTok Web this is an HTTP long-poll:
    // the response carries a batch of messages plus a cursor/internal_ext to
    // pass to the next fetch. (The dedicated WebSocket push_server is no longer
    // returned here, so we poll.)
    tiktok::ProtoMessageFetchResult fetch_once(const std::string& cursor,
                                               const std::string& internal_ext) {
        ParamList params = web_defaults::base_web_params();
        params.push_back({"room_id", std::to_string(room_id)});
        params.push_back({"resp_content_type", "protobuf"});
        params.push_back({"identity", "audience"});
        params.push_back({"live_id", "12"});
        params.push_back({"sup_ws_ds_opt", "1"});
        params.push_back({"cursor", cursor});
        params.push_back({"internal_ext", internal_ext});

        std::string url = signed_url(std::string(web_defaults::kWebcastUrl) + "/im/fetch/", params);
        HttpResponse resp = http.get(url, {{"Referer", "https://www.tiktok.com/@" + unique_id + "/live"}});

        if (resp.status != 200) {
            throw std::runtime_error(
                "im/fetch returned HTTP " + std::to_string(resp.status) +
                " (TikTok rejected the request — signature/cookies/fingerprint).");
        }

        sink("im_fetch", resp.body.data(), resp.body.size());

        tiktok::ProtoMessageFetchResult result;
        if (!result.ParseFromString(resp.body)) {
            throw std::runtime_error(
                "Failed to parse im/fetch protobuf response (empty or blocked)");
        }
        return result;
    }

    // Turn a ProtoMessageFetchResult into events.
    bool process_fetch_result(const tiktok::ProtoMessageFetchResult& result) {
        bool live_ended = false;
        for (const auto& msg : result.messages()) {
            Event ev;
            const std::string& method = msg.method();
            std::vector<uint8_t> payload(msg.payload().begin(), msg.payload().end());
            sink("msg:" + method, payload.data(), payload.size());
            parse_event(method, payload, ev);
            emit(ev);
            if (ev.type == EventType::LiveEnd) live_ended = true;
        }
        return live_ended;
    }

    // ---- WebSocket transport ---------------------------------------------

    // Map the tt-target-idc cookie to a webcast-ws region host.
    std::string ws_host() const {
        std::string idc = http.cookie("tt-target-idc");
        // Observed hosts: webcast-ws.eu.tiktok.com (eu-*), webcast-ws.us.tiktok.com.
        if (idc.rfind("eu", 0) == 0) return "webcast-ws.eu.tiktok.com";
        if (idc.rfind("sg", 0) == 0 || idc.rfind("alisg", 0) == 0)
            return "webcast-ws.sg.tiktok.com";
        return "webcast-ws.us.tiktok.com";
    }

    // Build + sign the webcast WS URL (returns wss://...).
    std::string build_ws_url(const std::string& cursor, const std::string& internal_ext) {
        ParamList params = web_defaults::base_ws_params();
        params.push_back({"room_id", std::to_string(room_id)});
        params.push_back({"compress", "gzip"});
        if (!cursor.empty()) params.push_back({"cursor", cursor});
        if (!internal_ext.empty()) params.push_back({"internal_ext", internal_ext});

        std::string base = "https://" + ws_host() +
                           "/webcast/im/ws_proxy/ws_reuse_supplement/?" +
                           web_defaults::encode_query(params);
        signer->set_cookies(http.cookie_header());
        std::string signed_url = signer->sign(base);
        // Switch scheme to wss for the WebSocket connect.
        if (signed_url.rfind("https://", 0) == 0)
            signed_url = "wss://" + signed_url.substr(8);
        return signed_url;
    }

    std::vector<uint8_t> make_push_frame(const std::string& payload_type,
                                         const std::vector<uint8_t>& payload,
                                         int64_t log_id = 0) {
        tiktok::WebcastPushFrame frame;
        if (log_id) frame.set_log_id(log_id);
        frame.set_payload_type(payload_type);
        frame.set_payload_encoding("pb");
        frame.set_payload(bytes_to_string(payload));
        std::string out;
        frame.SerializeToString(&out);
        return string_to_bytes(out);
    }

    void ws_send_enter_room() {
        tiktok::WebcastImEnterRoomMessage msg;
        msg.set_room_id(room_id);
        msg.set_identity("audience");
        msg.set_cursor("");
        std::string body;
        msg.SerializeToString(&body);
        ws.send_binary(make_push_frame("im_enter_room", string_to_bytes(body)));
    }

    void ws_send_heartbeat() {
        tiktok::HeartBeatMessage hb;
        hb.set_room_id(room_id);
        hb.set_send_packet_seq_id(hb_seq++);
        std::string body;
        hb.SerializeToString(&body);
        ws.send_binary(make_push_frame("hb", string_to_bytes(body)));
    }

    void ws_send_ack(int64_t log_id, const std::string& internal_ext) {
        std::string payload = internal_ext.empty() ? "-" : internal_ext;
        ws.send_binary(make_push_frame("ack", string_to_bytes(payload), log_id));
    }

    // Decode an incoming WebcastPushFrame; if it's a "msg" frame, unwrap the
    // (optionally gzipped) ProtoMessageFetchResult and dispatch its events.
    // Returns true if the live ended.
    bool ws_handle_frame(const std::vector<uint8_t>& raw) {
        // Sink the frame *before* any parsing so even undecodable frames are
        // captured in debug dumps.
        sink("ws_frame", raw.data(), raw.size());
        tiktok::WebcastPushFrame frame;
        if (!frame.ParseFromArray(raw.data(), static_cast<int>(raw.size()))) return false;
        if (frame.payload_type() != "msg") return false;

        std::string payload = frame.payload();
        for (const auto& h : frame.headers()) {
            if (h.key() == "compress_type" && h.value() == "gzip") {
                payload = gunzip(payload);
                break;
            }
        }
        // Auto-detect gzip even without the header.
        if (payload.size() > 2 && (unsigned char)payload[0] == 0x1f &&
            (unsigned char)payload[1] == 0x8b) {
            payload = gunzip(payload);
        }

        tiktok::ProtoMessageFetchResult result;
        if (!result.ParseFromString(payload)) return false;

        if (result.need_ack()) ws_send_ack(frame.log_id(), result.internal_ext());
        return process_fetch_result(result);
    }

    // Run the WebSocket event stream. Returns true if it ran (connected);
    // false if the handshake failed (caller should fall back to polling).
    bool run_websocket(const tiktok::ProtoMessageFetchResult& initial) {
        std::string url;
        try {
            url = build_ws_url(initial.cursor(), initial.internal_ext());
            ws.connect(url, http.cookie_header(), http.user_agent());
        } catch (const std::exception&) {
            return false;
        }

        ws_send_enter_room();

        auto last_hb = std::chrono::steady_clock::now();
        const auto hb_interval = std::chrono::seconds(10);

        while (running.load() && !stop_requested.load() && ws.connected()) {
            std::vector<uint8_t> msg;
            WsClient::RecvStatus st = ws.recv_binary(msg);
            if (st == WsClient::RecvStatus::Message) {
                if (ws_handle_frame(msg)) break;  // live ended
            } else if (st == WsClient::RecvStatus::Again) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } else {
                break;  // Closed or Error
            }

            auto now = std::chrono::steady_clock::now();
            if (now - last_hb >= hb_interval) {
                ws_send_heartbeat();
                last_hb = now;
            }
        }
        ws.close();
        return true;
    }

    void run() {
        running.store(true);
        stop_requested.store(false);

        // 0) Warm up: hit the homepage so TikTok issues a ttwid cookie (the
        //    Chrome-fingerprinted transport gets past the WAF). Best-effort.
        try {
            http.get(std::string(web_defaults::kAppUrl) + "/");
        } catch (const std::exception&) {
            // ignore; the fetch step will surface any real failure
        }

        // 1) Resolve room id.
        if (options.room_id_override != 0) {
            room_id = options.room_id_override;
        } else {
            try {
                room_id = fetch_room_id_from_html();
            } catch (const std::exception&) {
                room_id = 0;
            }
            if (room_id == 0) room_id = fetch_room_id_from_api();
        }
        if (room_id == 0)
            throw std::runtime_error("Could not resolve room id for @" + unique_id);

        // 2) Live check.
        if (options.fetch_live_check && !fetch_is_live()) {
            throw std::runtime_error("User @" + unique_id + " is not live");
        }

        // 2b) Stream playback URLs (FLV/HLS/RTMP by quality). Best-effort.
        if (options.fetch_stream_info) {
            try {
                stream_info = fetch_room_info();
            } catch (const std::exception&) {
                // ignore; stream_info stays empty
            }
        }

        // 2c) Gift list (optional; large response). Best-effort.
        if (options.fetch_gift_list) {
            try {
                gift_list = fetch_gift_list_impl();
            } catch (const std::exception&) {
                // ignore; gift_list stays empty
            }
        }

        // 3) Initial fetch.
        tiktok::ProtoMessageFetchResult initial = fetch_once("", "");

        // 4) Emit Connect.
        {
            Event ev;
            ev.type = EventType::Connect;
            ev.method = "Connect";
            ev.unique_id = unique_id;
            ev.room_id = room_id;
            ev.stream = stream_info;
            emit(ev);
        }

        // 5) Emit the initial backlog (if requested).
        if (options.process_connect_events) {
            process_fetch_result(initial);
        }

        // 6) Real-time transport: WebSocket first (low latency), falling back
        //    to HTTP long-polling if the WS handshake fails.
        bool ws_ran = false;
        if (options.use_websocket) {
            ws_ran = run_websocket(initial);
        }
        if (!ws_ran) {
            poll_loop(initial);
        }

        // 7) Teardown.
        running.store(false);

        Event ev;
        ev.type = EventType::Disconnect;
        ev.method = "Disconnect";
        emit(ev);
    }

    // HTTP long-poll loop (fallback). Re-fetches with the rolling cursor at the
    // server-provided fetch_interval.
    void poll_loop(const tiktok::ProtoMessageFetchResult& initial) {
        std::string cursor = initial.cursor();
        std::string internal_ext = initial.internal_ext();
        int64_t interval_ms = initial.fetch_interval() > 0 ? initial.fetch_interval() : 1000;

        while (running.load() && !stop_requested.load()) {
            for (int64_t slept = 0; slept < interval_ms && running.load() && !stop_requested.load();
                 slept += 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    std::min<int64_t>(100, interval_ms - slept)));
            }
            if (!running.load() || stop_requested.load()) break;

            tiktok::ProtoMessageFetchResult batch;
            try {
                batch = fetch_once(cursor, internal_ext);
            } catch (const std::exception&) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            bool live_ended = process_fetch_result(batch);

            if (!batch.cursor().empty()) cursor = batch.cursor();
            if (!batch.internal_ext().empty()) internal_ext = batch.internal_ext();
            if (batch.fetch_interval() > 0) interval_ms = batch.fetch_interval();

            if (live_ended) break;
        }
    }
};

TikTokLiveClient::TikTokLiveClient(std::string unique_id, ClientOptions options)
    : impl_(new Impl()) {
    impl_->unique_id = clean_unique_id(std::move(unique_id));
    impl_->options = std::move(options);

    // Construct the QuickJS signer (uses the compile-time JS dir unless
    // overridden in options).
    impl_->signer.reset(new QuickJsSigner(
        impl_->options.js_dir.empty() ? QuickJsSigner::default_js_dir()
                                      : impl_->options.js_dir));

    // Seed cookies from "name=val; name=val" if provided.
    const std::string& ck = impl_->options.cookies;
    size_t pos = 0;
    while (pos < ck.size()) {
        size_t semi = ck.find(';', pos);
        std::string pair = ck.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        // trim
        while (!pair.empty() && pair.front() == ' ') pair.erase(pair.begin());
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            impl_->http.set_cookie(pair.substr(0, eq), pair.substr(eq + 1));
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
}

TikTokLiveClient::~TikTokLiveClient() {
    disconnect();
}

void TikTokLiveClient::on(EventType type, EventCallback cb) {
    impl_->handlers[type].push_back(std::move(cb));
}

void TikTokLiveClient::on_any(EventCallback cb) {
    impl_->any_handlers.push_back(std::move(cb));
}

void TikTokLiveClient::run() { impl_->run(); }

void TikTokLiveClient::disconnect() {
    impl_->stop_requested.store(true);
    impl_->running.store(false);
    impl_->ws.close();
}

int64_t TikTokLiveClient::room_id() const { return impl_->room_id; }

const std::string& TikTokLiveClient::unique_id() const { return impl_->unique_id; }

const StreamInfo& TikTokLiveClient::stream_info() const { return impl_->stream_info; }

const std::vector<GiftInfo>& TikTokLiveClient::gift_list() const { return impl_->gift_list; }

const std::vector<GiftInfo>& TikTokLiveClient::fetch_gift_list() {
    impl_->gift_list = impl_->fetch_gift_list_impl();
    return impl_->gift_list;
}

}  // namespace ttlive
