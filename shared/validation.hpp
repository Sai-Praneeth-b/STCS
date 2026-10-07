// validation.hpp -- field rules from the Phase 1 data design, as plain
// functions on strings.
//
// Both programs use these: the client validates what the user typed before
// sending (FR-25, NFR-07) and the server re-validates every received field
// (NFR-14). Sharing one implementation guarantees both sides agree.
#pragma once

#include <cstddef>
#include <string>

namespace stcs {

constexpr std::size_t kMaxDisplayNameChars = 24;
constexpr std::size_t kMaxTitleChars = 120;

// Removes leading/trailing ASCII whitespace (space, \t, \n, \r, \v, \f).
std::string trim_ascii(const std::string& text);

// Display name: 1-24 characters from letters, digits, space, '_', '-', '.'
// (letters are ASCII A-Z / a-z). It must not start or end with a space.
// On failure `reason` is a short phrase such as "must not be empty".
bool validate_display_name(const std::string& name, std::string& reason);

// Title: trimmed first, then 1-120 characters (Unicode code points), not
// blank, no control characters. On success `normalized` holds the trimmed
// title that the server stores.
bool validate_title(const std::string& raw, std::string& normalized, std::string& reason);

// Client-side helpers for text typed by the user (the server never sees this
// text; it receives typed JSON fields instead).
//   task id : decimal digits only, value >= 1, fits in 63 bits
//   port    : decimal digits only, 1-65535
bool parse_task_id_text(const std::string& text, long long& task_id, std::string& reason);
bool parse_port_text(const std::string& text, int& port, std::string& reason);

}  // namespace stcs
