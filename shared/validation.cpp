// validation.cpp -- see validation.hpp.
#include "shared/validation.hpp"

#include <cerrno>
#include <cstdlib>

#include "shared/utf8.hpp"

namespace stcs {

namespace {

bool is_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool is_name_character(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == ' ' || c == '_' || c == '-' || c == '.';
}

bool all_digits(const std::string& text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

}  // namespace

std::string trim_ascii(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_ascii_space(text[begin])) ++begin;
    while (end > begin && is_ascii_space(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

bool validate_display_name(const std::string& name, std::string& reason) {
    if (name.empty()) {
        reason = "must not be empty";
        return false;
    }
    if (name.size() > kMaxDisplayNameChars) {
        reason = "must be at most 24 characters";
        return false;
    }
    for (char c : name) {
        if (!is_name_character(c)) {
            reason = "may contain only letters, digits, space, '_', '-' and '.'";
            return false;
        }
    }
    if (name.front() == ' ' || name.back() == ' ') {
        reason = "must not start or end with a space";
        return false;
    }
    return true;
}

bool validate_title(const std::string& raw, std::string& normalized, std::string& reason) {
    if (!is_valid_utf8(raw)) {
        reason = "must be valid UTF-8 text";
        return false;
    }
    const std::string trimmed = trim_ascii(raw);
    if (trimmed.empty()) {
        reason = "must not be blank";
        return false;
    }
    std::size_t pos = 0;
    std::size_t characters = 0;
    std::uint32_t code_point = 0;
    while (pos < trimmed.size()) {
        if (!decode_utf8(trimmed, pos, code_point)) {
            reason = "must be valid UTF-8 text";
            return false;
        }
        if (is_control_code_point(code_point)) {
            reason = "must not contain control characters";
            return false;
        }
        ++characters;
    }
    if (characters > kMaxTitleChars) {
        reason = "must be at most 120 characters";
        return false;
    }
    normalized = trimmed;
    return true;
}

bool parse_task_id_text(const std::string& text, long long& task_id, std::string& reason) {
    if (!all_digits(text)) {
        reason = "must be a whole number such as 3";
        return false;
    }
    errno = 0;
    const long long value = std::strtoll(text.c_str(), nullptr, 10);
    if (errno == ERANGE) {
        reason = "is too large";
        return false;
    }
    if (value < 1) {
        reason = "must be 1 or greater";
        return false;
    }
    task_id = value;
    return true;
}

bool parse_port_text(const std::string& text, int& port, std::string& reason) {
    if (!all_digits(text)) {
        reason = "must be a whole number between 1 and 65535";
        return false;
    }
    if (text.size() > 5) {  // avoids overflow before the range check
        reason = "must be between 1 and 65535";
        return false;
    }
    const long value = std::strtol(text.c_str(), nullptr, 10);
    if (value < 1 || value > 65535) {
        reason = "must be between 1 and 65535";
        return false;
    }
    port = static_cast<int>(value);
    return true;
}

}  // namespace stcs
