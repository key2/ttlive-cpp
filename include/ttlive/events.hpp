#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ttlive {

/// A single playable stream quality (e.g. quality="HD1", label="720p").
struct StreamQuality {
    std::string quality;   ///< TikTok quality key: SD1, SD2, HD1, FULL_HD1, ...
    std::string label;     ///< Human label: "360p", "720p", "1080p", ...
    std::string flv_url;   ///< FLV pull URL for this quality (may be empty)
    std::string hls_url;   ///< HLS pull URL for this quality (may be empty)
};

/// Live stream playback info scraped from /webcast/room/info/.
struct StreamInfo {
    std::string default_quality;              ///< e.g. "SD1"
    std::vector<StreamQuality> qualities;     ///< ordered, best-effort
    std::string rtmp_pull_url;                ///< single RTMP URL (if any)

    /// Convenience: FLV URL for a quality key, or "" if absent.
    std::string flv(const std::string& quality) const {
        for (const auto& q : qualities)
            if (q.quality == quality) return q.flv_url;
        return {};
    }
    /// Convenience: the default quality's FLV URL (falls back to first).
    std::string best_flv() const {
        std::string d = flv(default_quality);
        if (!d.empty()) return d;
        for (const auto& q : qualities)
            if (!q.flv_url.empty()) return q.flv_url;
        return {};
    }
    bool empty() const { return qualities.empty() && rtmp_pull_url.empty(); }
};

/// A TikTok user, distilled from the Webcast ``User`` protobuf to the fields
/// most consumers care about.
struct User {
    int64_t id = 0;
    std::string unique_id;   ///< @handle (display_id)
    std::string nickname;    ///< Display name
    bool verified = false;
};

/// A gift definition from the room's gift list (/webcast/gift/list/).
struct GiftInfo {
    int64_t id = 0;
    std::string name;
    int32_t diamond_count = 0;   ///< value in diamonds
    std::string describe;        ///< e.g. "sent Rose"
    int32_t type = 0;            ///< gift type (1 = streakable, ...)
    std::string icon_url;        ///< first CDN icon URL, if any
};

/// The kind of event received from the stream. ``Unknown`` covers any Webcast
/// message we don't specifically model (the raw method name is still exposed
/// on the event).
enum class EventType {
    Connect,
    Disconnect,
    Comment,
    Gift,
    Like,
    Join,      ///< Member entered the room
    Follow,
    Share,
    Subscribe,
    RoomUserSeq, ///< Viewer count / top viewers update
    Control,     ///< Stream started/paused/ended, etc.
    LiveEnd,
    Unknown
};

const char* to_string(EventType type);

/// A single parsed event delivered to the user's callback.
///
/// Only the fields relevant to ``type`` are populated. ``method`` always holds
/// the underlying Webcast message name (e.g. "WebcastChatMessage") and
/// ``raw_payload`` holds the undecoded protobuf bytes for that message so
/// advanced consumers can decode fields we didn't surface.
struct Event {
    EventType type = EventType::Unknown;
    std::string method;

    // Who triggered it (when applicable).
    User user;

    // Comment
    std::string comment;

    // Gift
    int64_t gift_id = 0;
    std::string gift_name;
    int32_t repeat_count = 0;
    bool gift_streaking = false;   ///< true while a combo is still in progress
    int32_t diamond_count = 0;
    int32_t gift_type = 0;         ///< gift type from the embedded Gift (1 = streakable)

    // Like
    int32_t like_count = 0;
    int64_t total_likes = 0;

    // Join / follow / share / room stats
    int64_t member_count = 0;
    int64_t viewer_count = 0;

    // Control
    int32_t control_action = 0;

    // Connect
    std::string unique_id;
    int64_t room_id = 0;
    StreamInfo stream;   ///< Populated on the Connect event.

    // Raw protobuf payload of the underlying message (may be empty).
    std::vector<uint8_t> raw_payload;
};

}  // namespace ttlive
