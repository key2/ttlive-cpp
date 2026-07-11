#pragma once

#include <map>
#include <string>

#include "web_defaults.hpp"

namespace ttlive {

struct HttpResponse {
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;  // lower-cased keys
};

/// HTTPS client built on libcurl-impersonate (BoringSSL) with a Chrome TLS /
/// HTTP2 / header fingerprint. This is what gets past TikTok's WAF, which
/// blocks OpenSSL-fingerprinted clients.
///
/// A shared cookie jar (in libcurl) persists ttwid / msToken across requests.
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /// Perform a GET. ``full_url`` is a complete https URL (query included).
    HttpResponse get(const std::string& full_url,
                     const std::map<std::string, std::string>& extra_headers = {});

    /// Cookie jar accessors (reads/writes libcurl's in-memory jar).
    void set_cookie(const std::string& name, const std::string& value);
    std::string cookie(const std::string& name) const;
    std::string cookie_header() const;

    const std::string& user_agent() const { return user_agent_; }

    /// The impersonation target (e.g. "chrome131").
    void set_impersonate(std::string target) { impersonate_ = std::move(target); }
    const std::string& impersonate() const { return impersonate_; }

private:
    struct Impl;
    Impl* impl_;
    std::string user_agent_;
    std::string impersonate_ = "chrome131";
};

/// Split a full https URL into (scheme, host, port, target).
struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string port;
    std::string target;  // path + query
};
ParsedUrl parse_url(const std::string& url);

}  // namespace ttlive
