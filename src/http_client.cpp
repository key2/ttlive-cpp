#include "http_client.hpp"

#include "web_defaults.hpp"

#include <curl/curl.h>

#include <cctype>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace ttlive {

namespace {

std::once_flag g_curl_init;

std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    size_t len = size * nitems;
    std::string line(buffer, len);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name = to_lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        // trim
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                                  s.back() == ' ' || s.back() == '\t')) s.pop_back();
        };
        trim(name);
        trim(value);
        if (!name.empty()) (*headers)[name] = value;
    }
    return len;
}

}  // namespace

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl out;
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) throw std::runtime_error("bad url: " + url);
    out.scheme = url.substr(0, scheme_end);
    std::string rest = url.substr(scheme_end + 3);
    auto slash = rest.find('/');
    std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out.target = (slash == std::string::npos) ? "/" : rest.substr(slash);
    auto colon = authority.find(':');
    if (colon == std::string::npos) {
        out.host = authority;
        out.port = (out.scheme == "http" || out.scheme == "ws") ? "80" : "443";
    } else {
        out.host = authority.substr(0, colon);
        out.port = authority.substr(colon + 1);
    }
    if (out.target.empty()) out.target = "/";
    return out;
}

struct HttpClient::Impl {
    CURL* curl = nullptr;

    Impl() {
        std::call_once(g_curl_init, [] { curl_global_init(CURL_GLOBAL_ALL); });
        curl = curl_easy_init();
        if (!curl) throw std::runtime_error("HttpClient: curl_easy_init failed");
        // Enable the in-memory cookie engine so ttwid/msToken persist.
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
        // CA bundle for TLS verification (required on Windows, where the
        // curl-impersonate DLL has no OS trust-store integration).
        const std::string& ca = web_defaults::ca_bundle_path();
        if (!ca.empty()) curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
    }
    ~Impl() {
        if (curl) curl_easy_cleanup(curl);
    }
};

HttpClient::HttpClient() : impl_(new Impl()) {
    // The impersonation target picks the UA; the SDK signer must use the same
    // UA. chrome131 => Chrome 131 on the target platform. We hard-code the UA
    // string used by the signer's fake DOM (Mac Chrome 131) and set it
    // explicitly so both sides match regardless of the platform curl runs on.
    user_agent_ =
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
}

HttpClient::~HttpClient() { delete impl_; }

void HttpClient::set_cookie(const std::string& name, const std::string& value) {
    // Set a cookie in libcurl's jar for the .tiktok.com domain.
    // Format: "Set-Cookie: name=value; domain=.tiktok.com; path=/"
    std::string line = "Set-Cookie: " + name + "=" + value +
                       "; domain=.tiktok.com; path=/";
    curl_easy_setopt(impl_->curl, CURLOPT_COOKIELIST, line.c_str());
}

std::string HttpClient::cookie_header() const {
    std::string out;
    struct curl_slist* cookies = nullptr;
    if (curl_easy_getinfo(impl_->curl, CURLINFO_COOKIELIST, &cookies) == CURLE_OK && cookies) {
        // Each entry is a Netscape-format line:
        // domain \t flag \t path \t secure \t expiry \t name \t value
        for (struct curl_slist* c = cookies; c; c = c->next) {
            std::string line = c->data;
            // split by tab
            std::vector<std::string> parts;
            size_t pos = 0, tab;
            while ((tab = line.find('\t', pos)) != std::string::npos) {
                parts.push_back(line.substr(pos, tab - pos));
                pos = tab + 1;
            }
            parts.push_back(line.substr(pos));
            if (parts.size() >= 7) {
                if (!out.empty()) out += "; ";
                out += parts[5] + "=" + parts[6];
            }
        }
        curl_slist_free_all(cookies);
    }
    return out;
}

std::string HttpClient::cookie(const std::string& name) const {
    std::string all = cookie_header();
    size_t pos = 0;
    while (pos < all.size()) {
        size_t semi = all.find(';', pos);
        std::string pair = all.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        while (!pair.empty() && pair.front() == ' ') pair.erase(pair.begin());
        auto eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == name)
            return pair.substr(eq + 1);
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return {};
}

HttpResponse HttpClient::get(const std::string& full_url,
                             const std::map<std::string, std::string>& extra_headers) {
    CURL* c = impl_->curl;
    HttpResponse resp;

    curl_easy_reset(c);
    curl_easy_setopt(c, CURLOPT_COOKIEFILE, "");  // keep cookie engine on
    curl_easy_setopt(c, CURLOPT_URL, full_url.c_str());

    // Re-apply after curl_easy_reset: CA bundle for TLS verification
    // (required on Windows; see web_defaults::ca_bundle_path).
    const std::string& ca = web_defaults::ca_bundle_path();
    if (!ca.empty()) curl_easy_setopt(c, CURLOPT_CAINFO, ca.c_str());

    // The one call that sets Chrome's TLS/HTTP2/header fingerprints.
    curl_easy_impersonate(c, impersonate_.c_str(), 1L);

    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");  // let curl decompress
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &resp.headers);

    // Extra headers (Referer etc.). We append rather than replace so the
    // impersonation's header set stays intact.
    struct curl_slist* hdrs = nullptr;
    for (const auto& kv : extra_headers) {
        std::string line = kv.first + ": " + kv.second;
        hdrs = curl_slist_append(hdrs, line.c_str());
    }
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = curl_easy_perform(c);
    if (hdrs) curl_slist_free_all(hdrs);

    if (rc != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(rc) +
                                 " (" + full_url.substr(0, 80) + ")");
    }
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    resp.status = static_cast<int>(code);
    return resp;
}

}  // namespace ttlive
