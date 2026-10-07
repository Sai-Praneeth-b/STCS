// utf8.cpp -- see utf8.hpp.
#include "shared/utf8.hpp"

namespace stcs {

bool decode_utf8(const std::string& s, std::size_t& pos, std::uint32_t& code_point) {
    if (pos >= s.size()) {
        return false;
    }
    const unsigned char lead = static_cast<unsigned char>(s[pos]);
    if (lead < 0x80) {
        code_point = lead;
        pos += 1;
        return true;
    }

    std::size_t length = 0;
    std::uint32_t value = 0;
    std::uint32_t smallest_allowed = 0;  // rejects overlong encodings
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        value = lead & 0x1F;
        smallest_allowed = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        value = lead & 0x0F;
        smallest_allowed = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        value = lead & 0x07;
        smallest_allowed = 0x10000;
    } else {
        return false;  // stray continuation byte or invalid lead byte
    }

    if (pos + length > s.size()) {
        return false;  // truncated sequence
    }
    for (std::size_t i = 1; i < length; ++i) {
        const unsigned char continuation = static_cast<unsigned char>(s[pos + i]);
        if ((continuation & 0xC0) != 0x80) {
            return false;
        }
        value = (value << 6) | (continuation & 0x3F);
    }

    const bool is_surrogate = value >= 0xD800 && value <= 0xDFFF;
    if (value < smallest_allowed || value > 0x10FFFF || is_surrogate) {
        return false;
    }
    code_point = value;
    pos += length;
    return true;
}

bool is_valid_utf8(const std::string& s) {
    std::size_t pos = 0;
    std::uint32_t ignored = 0;
    while (pos < s.size()) {
        if (!decode_utf8(s, pos, ignored)) {
            return false;
        }
    }
    return true;
}

std::size_t utf8_length(const std::string& s) {
    std::size_t pos = 0;
    std::size_t count = 0;
    std::uint32_t ignored = 0;
    while (pos < s.size()) {
        if (!decode_utf8(s, pos, ignored)) {
            ++pos;  // invalid byte: count it so the result is still bounded
        }
        ++count;
    }
    return count;
}

void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point < 0x80) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

std::string utf8_truncate(const std::string& s, std::size_t max_chars) {
    std::size_t pos = 0;
    std::size_t count = 0;
    std::uint32_t ignored = 0;
    while (pos < s.size() && count < max_chars) {
        if (!decode_utf8(s, pos, ignored)) {
            break;  // stop at the first malformed byte instead of copying it
        }
        ++count;
    }
    return s.substr(0, pos);
}

bool is_control_code_point(std::uint32_t code_point) {
    return code_point < 0x20 || (code_point >= 0x7F && code_point <= 0x9F);
}

}  // namespace stcs
