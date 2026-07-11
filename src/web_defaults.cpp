#include "web_defaults.hpp"

namespace ttlive {
namespace web_defaults {

ParamList base_web_params() {
    return {
        {"aid", "1988"},
        {"app_language", "en-US"},
        {"app_name", "tiktok_web"},
        {"browser_language", "en-US"},
        {"browser_name", "Mozilla"},
        {"browser_online", "true"},
        {"browser_platform", "Win32"},
        {"browser_version", "5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"},
        {"cookie_enabled", "true"},
        {"device_platform", "web_pc"},
        {"focus_state", "true"},
        {"from_page", "user"},
        {"history_len", "4"},
        {"is_fullscreen", "false"},
        {"is_page_visible", "true"},
        {"screen_height", "1080"},
        {"screen_width", "1920"},
        {"tz_name", "America/New_York"},
        {"channel", "tiktok_web"},
        {"data_collection_enabled", "true"},
        {"os", "windows"},
        {"priority_region", "US"},
        {"region", "US"},
        {"user_is_login", "false"},
        {"webcast_language", "en-US"},
    };
}

// Params for the webcast push WebSocket, matching the browser (golive2 HAR).
// room_id / compress / cursor / internal_ext / X-Bogus are added by the client.
ParamList base_ws_params() {
    return {
        {"version_code", "270000"},
        {"device_platform", "web"},
        {"cookie_enabled", "true"},
        {"app_name", "tiktok_web"},
        {"aid", "1988"},
        {"live_id", "12"},
        {"identity", "audience"},
        {"sup_ws_ds_opt", "1"},
        {"ws_direct", "1"},
        {"resp_content_type", "protobuf"},
        {"did_rule", "3"},
        {"heartbeat_duration", "10000"},
        {"last_rtt", "0"},
        {"app_language", "en"},
        {"webcast_language", "en"},
        {"client_enter", "1"},
        {"update_version_code", "2.0.0"},
        {"browser_name", "Mozilla"},
        {"browser_online", "true"},
    };
}

static void append_encoded(std::string& out, const std::string& s) {
    // Percent-encode anything that isn't an RFC3986 unreserved char. TikTok
    // query values (esp. browser_version / user_agent) contain spaces, parens
    // and semicolons that MUST be encoded or the webcast host returns HTTP 400.
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
}

std::string encode_query(const ParamList& params) {
    std::string out;
    bool first = true;
    for (const auto& kv : params) {
        if (!first) out += "&";
        first = false;
        append_encoded(out, kv.first);
        out += "=";
        append_encoded(out, kv.second);
    }
    return out;
}

}  // namespace web_defaults
}  // namespace ttlive
