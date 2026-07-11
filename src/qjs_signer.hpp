#pragma once

#include <string>

namespace ttlive {

/// Signs TikTok URLs by running TikTok's real ``webmssdk.js`` SDK inside an
/// embedded QuickJS engine and intercepting its XHR to read the generated
/// ``X-Bogus`` / ``X-Gnarly`` query parameters.
///
/// This is the signing path TikTok actually accepts (verified against live
/// traffic), unlike the reverse-engineered ``x_bogus``/``x_gnarly`` C code.
///
/// The engine is initialized lazily on first use and reused across calls.
/// A single instance is NOT thread-safe (QuickJS contexts are single-threaded);
/// use one Signer per thread.
class QuickJsSigner {
public:
    /// @param js_dir Directory containing the SDK JS files (hybrid-fake-dom.js,
    ///               dw-index.js, browser.sg.js, webmssdk.js, webmssdk_ex.js,
    ///               secsdk-lastest.umd.js). Defaults to the compile-time
    ///               TTLIVE_JS_DIR.
    explicit QuickJsSigner(std::string js_dir = default_js_dir());
    ~QuickJsSigner();

    QuickJsSigner(const QuickJsSigner&) = delete;
    QuickJsSigner& operator=(const QuickJsSigner&) = delete;

    /// Inject the browser cookie string into the JS ``document.cookie`` so the
    /// SDK signs with the same cookies as the HTTP client (msToken etc.).
    void set_cookies(const std::string& cookie_header);

    /// Sign a full URL. Returns the URL with msToken(if present)/X-Bogus/
    /// X-Gnarly appended, ready to request. Throws std::runtime_error on
    /// initialization failure.
    std::string sign(const std::string& url);

    /// The default JS directory baked in at compile time.
    static std::string default_js_dir();

private:
    void ensure_initialized();

    struct Impl;
    Impl* impl_;
    std::string js_dir_;
    bool initialized_ = false;
};

}  // namespace ttlive
