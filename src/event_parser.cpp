#include "event_parser.hpp"

#include "tiktok.pb.h"

namespace ttlive {

namespace {

void fill_user(const tiktok::User& u, User& out) {
    out.id = u.id();
    out.unique_id = u.display_id();
    out.nickname = u.nickname();
    out.verified = u.verified();
    if (u.has_avatar_thumb() && u.avatar_thumb().url_list_size() > 0)
        out.avatar_url = u.avatar_thumb().url_list(0);
    else if (u.has_avatar_medium() && u.avatar_medium().url_list_size() > 0)
        out.avatar_url = u.avatar_medium().url_list(0);
}

std::string bytes_to_string(const std::vector<uint8_t>& p) {
    return std::string(reinterpret_cast<const char*>(p.data()), p.size());
}

}  // namespace

bool parse_event(const std::string& method,
                 const std::vector<uint8_t>& payload,
                 Event& out) {
    out.method = method;
    out.raw_payload = payload;
    const std::string data = bytes_to_string(payload);

    if (method == "WebcastChatMessage") {
        tiktok::WebcastChatMessage m;
        if (m.ParseFromString(data)) {
            out.type = EventType::Comment;
            fill_user(m.user(), out.user);
            out.comment = m.content();
            return true;
        }
    } else if (method == "WebcastGiftMessage") {
        tiktok::WebcastGiftMessage m;
        if (m.ParseFromString(data)) {
            out.type = EventType::Gift;
            fill_user(m.user(), out.user);
            out.gift_id = m.gift_id();
            out.repeat_count = m.repeat_count();
            // repeat_end == 0 while a streakable combo is in progress.
            out.gift_streaking = (m.repeat_end() == 0);
            if (m.has_gift()) {
                out.gift_name = m.gift().name();
                out.diamond_count = m.gift().diamond_count();
                out.gift_type = m.gift().type();
                // Icon URL straight from the event (image, then icon), used as
                // a fallback when the room's gift list omits this gift.
                const auto& g = m.gift();
                if (g.has_image() && g.image().url_list_size() > 0)
                    out.gift_icon_url = g.image().url_list(0);
                else if (g.has_icon() && g.icon().url_list_size() > 0)
                    out.gift_icon_url = g.icon().url_list(0);
            }
            return true;
        }
    } else if (method == "WebcastLikeMessage") {
        tiktok::WebcastLikeMessage m;
        if (m.ParseFromString(data)) {
            out.type = EventType::Like;
            fill_user(m.user(), out.user);
            out.like_count = m.count();
            out.total_likes = m.total();
            return true;
        }
    } else if (method == "WebcastMemberMessage") {
        tiktok::WebcastMemberMessage m;
        if (m.ParseFromString(data)) {
            out.type = EventType::Join;
            fill_user(m.user(), out.user);
            out.member_count = m.member_count();
            return true;
        }
    } else if (method == "WebcastSocialMessage") {
        tiktok::WebcastSocialMessage m;
        if (m.ParseFromString(data)) {
            fill_user(m.user(), out.user);
            // action/share_type distinguishes follow vs share; the display
            // marker in ``common.display_text`` is the authoritative source in
            // upstream, but action is a reliable proxy: 1 = follow, 3 = share.
            const int64_t action = m.action();
            if (action == 3 || m.share_type() != 0) {
                out.type = EventType::Share;
            } else {
                out.type = EventType::Follow;
            }
            out.member_count = m.follow_count();
            return true;
        }
    } else if (method == "WebcastRoomUserSeqMessage") {
        tiktok::WebcastRoomUserSeqMessage m;
        if (m.ParseFromString(data)) {
            out.type = EventType::RoomUserSeq;
            out.viewer_count = m.total();
            return true;
        }
    } else if (method == "WebcastSubNotifyMessage") {
        tiktok::WebcastSubNotifyMessage m;
        if (m.ParseFromString(data)) {
            out.type = EventType::Subscribe;
            fill_user(m.user(), out.user);
            return true;
        }
    } else if (method == "WebcastControlMessage") {
        tiktok::WebcastControlMessage m;
        if (m.ParseFromString(data)) {
            out.control_action = static_cast<int32_t>(m.action());
            // STREAM_ENDED (3) / STREAM_SUSPENDED (4) => the live is over.
            if (out.control_action == 3 || out.control_action == 4) {
                out.type = EventType::LiveEnd;
            } else {
                out.type = EventType::Control;
            }
            return true;
        }
    }

    out.type = EventType::Unknown;
    return false;
}

}  // namespace ttlive
