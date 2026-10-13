#pragma once

#include <cstddef>
#include <string>

namespace stcs {

enum class FrameStatus {
    Message,
    Blank,
    TooLarge,
};

struct Frame {
    FrameStatus status = FrameStatus::Message;
    std::string line;
};

class MessageFramer {
public:
    explicit MessageFramer(std::size_t max_line);

    void feed(const char* data, std::size_t length);

    bool next_frame(Frame& out);

    std::size_t buffered_bytes() const { return buffer_.size(); }

    bool discarding() const { return discarding_; }

private:
    std::size_t max_line_;
    std::string buffer_;
    bool discarding_ = false;
};

std::string frame_message(const std::string& json_text);

}
