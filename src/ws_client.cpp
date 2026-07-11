#include "ws_client.hpp"

#include <curl/curl.h>

#include <stdexcept>
#include <string>

namespace ttlive {

struct WsClient::Impl {
    CURL* curl = nullptr;
    struct curl_slist* hdrs = nullptr;
    // Reassembly buffer for fragmented frames.
    std::vector<uint8_t> partial;

    ~Impl() {
        if (hdrs) curl_slist_free_all(hdrs);
        if (curl) curl_easy_cleanup(curl);
    }
};

WsClient::WsClient() : impl_(new Impl()) {}

WsClient::~WsClient() {
    close();
    delete impl_;
}

void WsClient::connect(const std::string& wss_url,
                       const std::string& cookie_header,
                       const std::string& user_agent) {
    impl_->curl = curl_easy_init();
    if (!impl_->curl) throw std::runtime_error("WsClient: curl_easy_init failed");
    CURL* c = impl_->curl;

    curl_easy_setopt(c, CURLOPT_URL, wss_url.c_str());
    // Chrome TLS/HTTP fingerprint (same as the REST client).
    curl_easy_impersonate(c, "chrome131", 1L);

    // Establish the WebSocket, then drive frames manually.
    curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 2L);

    if (!cookie_header.empty()) {
        std::string ck = "Cookie: " + cookie_header;
        impl_->hdrs = curl_slist_append(impl_->hdrs, ck.c_str());
    }
    impl_->hdrs = curl_slist_append(impl_->hdrs, "Origin: https://www.tiktok.com");
    if (!user_agent.empty()) {
        std::string ua = "User-Agent: " + user_agent;
        impl_->hdrs = curl_slist_append(impl_->hdrs, ua.c_str());
    }
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, impl_->hdrs);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 0L);

    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(c);
        impl_->curl = nullptr;
        throw std::runtime_error(std::string("WebSocket connect failed: ") +
                                 curl_easy_strerror(rc) + " (HTTP " +
                                 std::to_string(code) + ")");
    }
    connected_.store(true);
}

bool WsClient::send_binary(const std::vector<uint8_t>& data) {
    if (!connected_.load() || !impl_->curl) return false;
    size_t sent = 0;
    const void* buf = data.empty() ? "" : static_cast<const void*>(data.data());
    CURLcode rc = curl_ws_send(impl_->curl, buf, data.size(), &sent, 0, CURLWS_BINARY);
    if (rc != CURLE_OK) {
        connected_.store(false);
        return false;
    }
    return true;
}

WsClient::RecvStatus WsClient::recv_binary(std::vector<uint8_t>& out) {
    if (!connected_.load() || !impl_->curl) return RecvStatus::Error;

    char buf[65536];
    size_t nread = 0;
    const struct curl_ws_frame* meta = nullptr;
    CURLcode rc = curl_ws_recv(impl_->curl, buf, sizeof(buf), &nread, &meta);

    if (rc == CURLE_AGAIN) return RecvStatus::Again;
    if (rc != CURLE_OK) {
        connected_.store(false);
        return RecvStatus::Error;
    }
    if (meta && (meta->flags & CURLWS_CLOSE)) {
        connected_.store(false);
        return RecvStatus::Closed;
    }

    // Accumulate this chunk.
    impl_->partial.insert(impl_->partial.end(), buf, buf + nread);

    // More of this frame still to come?
    if (meta && meta->bytesleft > 0) return RecvStatus::Again;
    // Fragmented message continued in a later frame?
    if (meta && (meta->flags & CURLWS_CONT)) return RecvStatus::Again;

    // Ping/pong are auto-handled by curl; only surface binary/text payloads.
    if (meta && (meta->flags & (CURLWS_BINARY | CURLWS_TEXT))) {
        out.swap(impl_->partial);
        impl_->partial.clear();
        return out.empty() ? RecvStatus::Again : RecvStatus::Message;
    }

    // Non-data control frame with nothing to deliver.
    impl_->partial.clear();
    return RecvStatus::Again;
}

void WsClient::close() {
    if (impl_->curl && connected_.load()) {
        size_t sent = 0;
        curl_ws_send(impl_->curl, "", 0, &sent, 0, CURLWS_CLOSE);
    }
    connected_.store(false);
}

}  // namespace ttlive
