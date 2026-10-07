// utf8.hpp -- minimal UTF-8 helpers shared by the STCS client and server.
//
// STCS messages are UTF-8 (Phase 1, section 7.2). These helpers let the JSON
// parser and the field validators reject malformed byte sequences instead of
// trusting the peer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace stcs {

// Decodes one code point that starts at s[pos]. On success the code point is
// stored in `code_point`, `pos` is advanced past it and true is returned.
// Returns false (leaving pos unchanged) for truncated, overlong, surrogate or
// out-of-range sequences.
bool decode_utf8(const std::string& s, std::size_t& pos, std::uint32_t& code_point);

// True if every byte of `s` belongs to a well-formed UTF-8 sequence.
bool is_valid_utf8(const std::string& s);

// Number of code points in `s` ("characters" in the Phase 1 length rules).
// The string must already be valid UTF-8.
std::size_t utf8_length(const std::string& s);

// Appends the UTF-8 encoding of `code_point` to `out`.
void append_utf8(std::string& out, std::uint32_t code_point);

// Returns at most `max_chars` code points of `s`, never cutting a multi-byte
// sequence in half (so the result is still valid UTF-8 when `s` is).
std::string utf8_truncate(const std::string& s, std::size_t max_chars);

// C0 controls (U+0000-U+001F), DEL (U+007F) and C1 controls (U+0080-U+009F).
bool is_control_code_point(std::uint32_t code_point);

}  // namespace stcs
