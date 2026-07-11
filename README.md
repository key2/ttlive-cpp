# ttlive-cpp

A cross-platform (Linux / macOS / Windows) C++ library + demo that connects to
a **TikTok LIVE** room and **receives** events — comments, gifts, likes, joins,
follows, shares, subscribes, viewer counts, and stream control — plus the
live **stream playback URLs** (FLV / RTMP by quality).

It is read-only: it does not send chat, gifts, or any other actions.

```
[CONNECTED] @icegirls_01 room_id=7661316147920571157
[STREAM] default=SD1
  HD1 (720p): https://pull-flv-...tiktokcdn.com/.../stream-..._hd.flv?...
  SD1 (360p): https://pull-flv-...tiktokcdn.com/.../stream-..._ld.flv?...
[CHAT] Jenny (@lunar_foxxy): Вау
[LIKE] rinnara +15 (total 2444)
[JOIN] Kuromi joined
[VIEWERS] 94 watching
```

---

## How it works

TikTok's Webcast API requires two things that a plain HTTPS client can't
provide, and this project solves both:

1. **Valid request signatures** (`X-Bogus` + `X-Gnarly`). These are produced by
   running TikTok's own `webmssdk.js` inside an embedded **QuickJS** engine and
   letting the SDK sign the request via its XHR interception. (Reverse-engineered
   reimplementations of the algorithm are rejected by TikTok; the real SDK is
   not.)

2. **A Chrome TLS/HTTP2 fingerprint.** TikTok's WAF fingerprints the TLS
   ClientHello (JA3/JA4), HTTP/2 settings, and header order, and blocks
   non-browser clients (OpenSSL etc.). The transport uses
   [**curl-impersonate**](https://github.com/lexiforest/curl-impersonate) — a
   BoringSSL-based libcurl fork that reproduces Chrome's fingerprint exactly.

### Connection flow

1. Warm up `https://www.tiktok.com/` so TikTok issues a `ttwid` cookie.
2. Resolve the room id from `/@user/live` (with an API fallback).
3. `GET /webcast/room/check_alive/` to confirm the user is live.
4. `GET /webcast/room/info/` → extract stream FLV/RTMP URLs by quality.
   (Optionally `GET /webcast/gift/list/` for the room's gift catalog.)
5. `GET /webcast/im/fetch/` (signed) → an initial batch of messages + a
   `cursor` / `internal_ext`.
6. **HTTP long-poll**: re-fetch with the latest cursor at the server-provided
   `fetch_interval`, decoding each protobuf message into an `Event`.

---

## Project layout

```
include/ttlive/
  client.hpp          Public API: TikTokLiveClient, ClientOptions
  events.hpp          Public API: Event, EventType, User, StreamInfo
src/
  client.cpp          Orchestration: warmup -> room id -> live check -> poll
  qjs_signer.*        QuickJS + webmssdk.js signer (X-Bogus / X-Gnarly)
  http_client.*       curl-impersonate HTTPS client (Chrome fingerprint, cookies)
  event_parser.*      Webcast protobuf message -> public Event
  web_defaults.*      Default query params + URL query encoder
js/                   TikTok's SDK JS (webmssdk.js, hybrid-fake-dom.js, ...)
proto/tiktok.proto    Webcast schema (generated from TikTokLiveProto v3)
third_party/quickjs/  bellard QuickJS (git submodule)
cmake/curl-impersonate.cmake   Downloads the Chrome-fingerprint libcurl fork
demo/main.cpp         CLI that prints events
tools/gen_proto.py    Regenerates proto/tiktok.proto
```

---

## Building

**Prerequisites**

- C++17 compiler, CMake ≥ 3.16
- OpenSSL, protobuf (libprotobuf + protoc), zlib
- git (for the QuickJS submodule)
- Internet access on first configure (CMake downloads the prebuilt
  curl-impersonate for your platform)

**Linux / macOS**

```sh
git clone <repo> && cd ttlive-cpp
git submodule update --init --recursive        # QuickJS
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ttlive_demo @icegirls_01
```

**Windows** (use vcpkg for OpenSSL/protobuf/zlib)

```powershell
git submodule update --init --recursive
vcpkg install openssl protobuf zlib
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
build\Release\ttlive_demo.exe @icegirls_01
```

**Offline / custom curl-impersonate.** If you already have a curl-impersonate
build (a directory with `include/curl/curl.h` and `libcurl-impersonate.a`),
skip the download:

```sh
cmake -B build -S . -DCURL_IMPERSONATE_LOCAL_DIR=/path/to/libcurl-impersonate
```

> The **static** curl-impersonate archive is linked deliberately. The prebuilt
> shared `.so` is built with zig and bundles its own `libunwind`, whose exported
> `_Unwind_*` symbols clash with libstdc++ and cause a crash on the first C++
> exception. The static archive avoids this.

---

## CLI usage

```
ttlive_demo <@username | username> [options]

  --room-id <id>       Skip room scraping, connect directly to a room id
  --no-live-check      Skip the is-live check
  --cookies "k=v;..."  Seed cookies (e.g. a logged-in ttwid / sessionid)
  --js-dir <path>      Directory holding the TikTok SDK JS files
  --gifts              Fetch and print the room gift list on connect
```

Examples:

```sh
./build/ttlive_demo @icegirls_01
./build/ttlive_demo icegirls_01 --room-id 7661316147920571157 --no-live-check
```

---

## Library usage

Link against the `ttlive` static library. The public API is in
`include/ttlive/` and does not leak protobuf, QuickJS, or curl headers.

```cpp
#include "ttlive/client.hpp"
#include <cstdio>

int main() {
    ttlive::TikTokLiveClient client("icegirls_01");   // or "@icegirls_01"

    client.on(ttlive::EventType::Connect, [](const ttlive::Event& e) {
        printf("connected to room %lld\n", (long long)e.room_id);
        // Stream playback URLs (by quality):
        for (const auto& q : e.stream.qualities)
            printf("  %s (%s): %s\n", q.quality.c_str(), q.label.c_str(),
                   q.flv_url.c_str());
        printf("best FLV: %s\n", e.stream.best_flv().c_str());
    });

    client.on(ttlive::EventType::Comment, [](const ttlive::Event& e) {
        printf("%s: %s\n", e.user.nickname.c_str(), e.comment.c_str());
    });

    client.on(ttlive::EventType::Gift, [](const ttlive::Event& e) {
        printf("%s sent %s x%d\n", e.user.nickname.c_str(),
               e.gift_name.c_str(), e.repeat_count);
    });

    client.run();   // blocks until the stream ends or disconnect()
}
```

### Events

`Event::type` is one of:

| EventType     | Populated fields                                             |
|---------------|--------------------------------------------------------------|
| `Connect`     | `unique_id`, `room_id`, `stream`                             |
| `Comment`     | `user`, `comment`                                            |
| `Gift`        | `user`, `gift_id`, `gift_name`, `repeat_count`, `gift_streaking`, `diamond_count` |
| `Like`        | `user`, `like_count`, `total_likes`                          |
| `Join`        | `user`, `member_count`                                       |
| `Follow`      | `user`                                                       |
| `Share`       | `user`                                                       |
| `Subscribe`   | `user`                                                       |
| `RoomUserSeq` | `viewer_count`                                               |
| `Control`     | `control_action`                                             |
| `LiveEnd`     | — (stream ended; the poll loop stops)                        |
| `Disconnect`  | — (emitted once when the loop exits)                         |
| `Unknown`     | `method`, `raw_payload` (any message we don't model)         |

Every event also carries `method` (the underlying Webcast message name) and
`raw_payload` (undecoded protobuf bytes) so you can decode fields we don't
surface. Use `client.on_any(cb)` to receive every event regardless of type.

### Stream URLs

`Event::stream` (on `Connect`) and `client.stream_info()` return a `StreamInfo`:

```cpp
struct StreamQuality { std::string quality, label, flv_url, hls_url; };
struct StreamInfo {
    std::string default_quality;              // e.g. "SD1"
    std::vector<StreamQuality> qualities;     // SD1/HD1/... present for the stream
    std::string rtmp_pull_url;
    std::string flv(const std::string& quality) const;  // FLV for a quality
    std::string best_flv() const;                       // default quality's FLV
    bool empty() const;
};
```

Quality keys map to labels via TikTok's `resolution_name`
(`SD1`=360p, `SD2`=540p, `HD1`=720p, `FULL_HD1`=1080p, `ORIGION`=Original, …);
which qualities exist varies per stream. The URLs are time-limited
(`expire=`/`sign=` params) — use them promptly.

### Gift list

The room's gift catalog (id, name, diamond value, icon) comes from
`/webcast/gift/list/`. It's large (hundreds of gifts / ~2 MB), so it's opt-in:

```cpp
ttlive::ClientOptions opts;
opts.fetch_gift_list = true;         // fetch on connect
ttlive::TikTokLiveClient client("icegirls_01", opts);
...
for (const auto& g : client.gift_list())
    printf("#%lld %s = %d diamonds\n", (long long)g.id,
           g.name.c_str(), g.diamond_count);

// Or fetch on demand any time after connecting:
const auto& gifts = client.fetch_gift_list();
```

```cpp
struct GiftInfo {
    int64_t id;
    std::string name;
    int32_t diamond_count;   // value in diamonds
    std::string describe;    // e.g. "sent Rose"
    int32_t type;            // gift type (1 = streakable, ...)
    std::string icon_url;    // first CDN icon URL
};
```

You can join this with `GiftEvent`s: `Event::gift_id` matches `GiftInfo::id`.

### ClientOptions

```cpp
struct ClientOptions {
    bool fetch_live_check = true;      // check is-live before connecting
    bool fetch_stream_info = true;     // fetch FLV/RTMP URLs on connect
    bool fetch_gift_list = false;      // fetch the gift catalog on connect (large)
    bool process_connect_events = true;// emit the initial backlog as events
    int64_t room_id_override = 0;      // connect directly to a room id
    std::string cookies;               // seed cookies "k=v; k=v"
    std::string js_dir;                // override the SDK JS directory
};
```

---

## Regenerating the protobuf schema

`proto/tiktok.proto` is generated from the `TikTokLiveProto` (betterproto2)
Python package:

```sh
python -m venv .venv && . .venv/bin/activate
pip install "betterproto2==0.9.*" pydantic
pip download TikTokLiveProto --no-deps -d /tmp/tt && (cd /tmp/tt && unzip -o *.whl -d extracted)
python tools/gen_proto.py /tmp/tt/extracted proto
```

---

## Notes & limitations

- **Stream URLs expire** quickly; re-fetch `room/info` if you need fresh ones.
- **HLS**: only the flat `hls_pull_url` is surfaced; the structured
  `hls_pull_url_map` is not parsed (most FLV-first streams leave it empty).
- **Updating the SDK**: replacing `js/webmssdk.js` with a newer build may break
  signing if it expects browser globals the `hybrid-fake-dom.js` shim doesn't
  provide — test before shipping.
- **Age/region-restricted rooms** may return limited data.
- This project is for interoperability/research. Respect TikTok's Terms of
  Service and applicable laws.
