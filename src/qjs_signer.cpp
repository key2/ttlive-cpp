#include "qjs_signer.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include "quickjs.h"
}

namespace ttlive {

namespace {

// SDK files loaded in order after the DOM shim.
const char* kSdkFiles[] = {
    "dw-index.js",
    "browser.sg.js",
    "webmssdk.js",
    "webmssdk_ex.js",
    "secsdk-lastest.umd.js",
};

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("QuickJsSigner: cannot open " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string json_quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += "\"";
    return out;
}

}  // namespace

struct QuickJsSigner::Impl {
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;

    ~Impl() {
        if (ctx) JS_FreeContext(ctx);
        if (rt) JS_FreeRuntime(rt);
    }

    // Evaluate code; throw with the JS exception message on failure.
    void eval(const std::string& code, const char* label, bool throw_on_exc = true) {
        JSValue r = JS_Eval(ctx, code.c_str(), code.size(), label, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) {
            JSValue ex = JS_GetException(ctx);
            const char* s = JS_ToCString(ctx, ex);
            std::string msg = s ? s : "(unknown JS exception)";
            JS_FreeCString(ctx, s);
            JS_FreeValue(ctx, ex);
            JS_FreeValue(ctx, r);
            if (throw_on_exc)
                throw std::runtime_error(std::string("QuickJsSigner ") + label + ": " + msg);
            return;
        }
        JS_FreeValue(ctx, r);
    }

    // Evaluate code and return its string result.
    std::string eval_str(const std::string& code, const char* label) {
        JSValue r = JS_Eval(ctx, code.c_str(), code.size(), label, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) {
            JSValue ex = JS_GetException(ctx);
            const char* s = JS_ToCString(ctx, ex);
            std::string msg = s ? s : "(unknown JS exception)";
            JS_FreeCString(ctx, s);
            JS_FreeValue(ctx, ex);
            JS_FreeValue(ctx, r);
            throw std::runtime_error(std::string("QuickJsSigner ") + label + ": " + msg);
        }
        const char* s = JS_ToCString(ctx, r);
        std::string out = s ? s : "";
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, r);
        return out;
    }

    void drain() {
        JSContext* c;
        for (int i = 0; i < 40; ++i) {
            if (JS_ExecutePendingJob(rt, &c) <= 0) break;
        }
        eval("if(typeof globalThis.__drainDeferredTimeouts==='function')"
             "globalThis.__drainDeferredTimeouts();",
             "drain-timeouts", /*throw_on_exc=*/false);
    }
};

QuickJsSigner::QuickJsSigner(std::string js_dir)
    : impl_(new Impl()), js_dir_(std::move(js_dir)) {}

QuickJsSigner::~QuickJsSigner() { delete impl_; }

std::string QuickJsSigner::default_js_dir() {
#ifdef TTLIVE_JS_DIR
    return TTLIVE_JS_DIR;
#else
    return "js";
#endif
}

void QuickJsSigner::ensure_initialized() {
    if (initialized_) return;

    impl_->rt = JS_NewRuntime();
    if (!impl_->rt) throw std::runtime_error("QuickJsSigner: JS_NewRuntime failed");
    JS_SetMaxStackSize(impl_->rt, 8 * 1024 * 1024);
    JS_SetMemoryLimit(impl_->rt, 512 * 1024 * 1024);
    impl_->ctx = JS_NewContext(impl_->rt);
    if (!impl_->ctx) throw std::runtime_error("QuickJsSigner: JS_NewContext failed");

    // stderr stub the DOM shim expects.
    impl_->eval("globalThis.__stderr={puts:function(s){}};", "stderr-stub");

    // Browser/DOM shim first.
    impl_->eval(read_file(js_dir_ + "/hybrid-fake-dom.js"), "hybrid-fake-dom.js");
    impl_->eval("globalThis.global=globalThis;", "global-alias");

    // SDK files.
    for (const char* name : kSdkFiles) {
        std::string path = js_dir_ + "/" + name;
        std::ifstream test(path);
        if (!test) continue;  // tolerate optional files
        impl_->eval(read_file(path), name);
    }
    impl_->drain();

    // Initialize byted_acrawler with XHR interception enabled.
    impl_->eval(
        "(function(){var ac=globalThis.byted_acrawler||"
        "(globalThis.window&&globalThis.window.byted_acrawler);"
        "if(!ac)throw new Error('byted_acrawler not found');"
        "ac.init({aid:1988,dfp:false,boe:false,intercept:true,"
        "enablePathList:['/webcast.*','/api/','/passport/','/ttwid/','/aweme/','/im/']});"
        "if(ac.setUserMode)ac.setUserMode('0x204');"
        "if(!globalThis.byted_acrawler&&globalThis.window&&globalThis.window.byted_acrawler)"
        "globalThis.byted_acrawler=globalThis.window.byted_acrawler;})();",
        "acrawler-init");
    impl_->drain();

    initialized_ = true;
}

void QuickJsSigner::set_cookies(const std::string& cookie_header) {
    ensure_initialized();
    std::string q = json_quote(cookie_header);
    impl_->eval(
        "try{document.cookie=" + q + ";}catch(e){}"
        "try{globalThis.window.document.cookie=" + q + ";}catch(e){}",
        "set-cookies", /*throw_on_exc=*/false);
}

std::string QuickJsSigner::sign(const std::string& url) {
    ensure_initialized();

    const std::string qurl = json_quote(url);
    std::string code =
        "(function(){"
        "var xhr=new XMLHttpRequest();"
        "xhr.open('GET'," + qurl + ");"
        "xhr.send();"
        "var u=xhr._url||" + qurl + ";"
        "var h=xhr._headers||{};"
        "var b=h[\"X-Bogus\"]||\"\";"
        "var g=h[\"X-Gnarly\"]||\"\";"
        "if(!b&&u.indexOf('X-Bogus=')>=0){"
        "  var m=u.match(/X-Bogus=([^&]*)/);if(m)b=decodeURIComponent(m[1]);"
        "  var m2=u.match(/X-Gnarly=([^&]*)/);if(m2)g=decodeURIComponent(m2[1]);"
        "}"
        // Ensure the returned URL carries both params.
        "if(b&&u.indexOf('X-Bogus=')<0){"
        "  u+=(u.indexOf('?')>=0?'&':'?')+'X-Bogus='+encodeURIComponent(b);"
        "  if(g)u+='&X-Gnarly='+encodeURIComponent(g);"
        "}"
        "return u;"
        "})();";

    std::string signed_url = impl_->eval_str(code, "sign");
    if (signed_url.empty()) {
        throw std::runtime_error("QuickJsSigner: signing produced an empty URL");
    }
    return signed_url;
}

}  // namespace ttlive
