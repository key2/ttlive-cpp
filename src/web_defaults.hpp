#pragma once

#include <string>
#include <vector>
#include <utility>

namespace ttlive {

// Ordered key/value list; order matters when we sign the query string.
using ParamList = std::vector<std::pair<std::string, std::string>>;

namespace web_defaults {

// Base URLs
inline constexpr const char* kAppUrl = "https://www.tiktok.com";
inline constexpr const char* kWebcastUrl = "https://webcast.tiktok.com/webcast";

// A realistic desktop Chrome UA (kept in one place so signing + requests match).
inline constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

/// Default query params sent with every Webcast REST request.
ParamList base_web_params();

/// Default query params for the webcast WebSocket connect URL.
ParamList base_ws_params();

/// Serialize a param list to ``k=v&k=v`` (no leading '?').
std::string encode_query(const ParamList& params);

/// Path to a CA certificate bundle usable as CURLOPT_CAINFO, or "" to keep
/// libcurl's built-in default. Needed on Windows: the curl-impersonate DLL
/// (BoringSSL) has no OS trust-store integration and no baked-in CA path, so
/// a ``curl-ca-bundle.crt`` / ``cacert.pem`` shipped next to the executable
/// (or pointed to by $CURL_CA_BUNDLE / $SSL_CERT_FILE) is used instead.
/// On Linux/macOS the static build's compiled-in default usually works and
/// this returns "" unless an override exists.
const std::string& ca_bundle_path();

}  // namespace web_defaults
}  // namespace ttlive
