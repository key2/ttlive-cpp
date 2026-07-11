#pragma once

#include <string>
#include <vector>

#include "ttlive/events.hpp"

namespace ttlive {

/// Parse a single Webcast message (identified by ``method``) with the given
/// protobuf ``payload`` into a public Event. Returns true if the method was
/// recognized and ``out`` populated; false for unknown methods (``out`` is
/// still filled as an Unknown event with the method + raw payload).
bool parse_event(const std::string& method,
                 const std::vector<uint8_t>& payload,
                 Event& out);

}  // namespace ttlive
