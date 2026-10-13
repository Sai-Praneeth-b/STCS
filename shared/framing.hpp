#include "shared/framing.hpp"

#include <stdexcept>

namespace stcs {

MessageFramer::MessageFramer(std::size_t max_line) : max_line_(max_line) {}

void MessageFramer::feed(const char* data, std::size_t length) {
    buffer_.append(data, length);
}

bool MessageFramer::next_frame(Frame& out) {
    while (true) {
        const std::size_t newline = buffer_.find('\n');

        if (discarding_) {
            if (newline == std::string::npos) {
                buffer_.clear();
                return false;
            }
            buffer_.erase(0, newline + 1);
            discarding_ = false;
            continue;
        }

        if (newline == std::string::npos) {
            if (buffer_.size() > max_line_) {
                buffer_.clear();
                discarding_ = true;
                out.status = FrameStatus::TooLarge;
                out.line.clear();
                return true;
            }
            return false;
        }

        if (newline > max_line_) {
            buffer_.erase(0, newline + 1);
            out.status = FrameStatus::TooLarge;
            out.line.clear();
            return true;
        }

        std::string line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            out.status = FrameStatus::Blank;
            out.line.clear();
        } else {
            out.status = FrameStatus::Message;
            out.line = std::move(line);
        }
        return true;
    }
}

std::string frame_message(const std::string& json_text) {
    if (json_text.find('\n') != std::string::npos) {
        throw std::logic_error("framed message must not contain a raw newline");
    }
    std::string framed = json_text;
    framed.push_back('\n');
    return framed;
}

}
