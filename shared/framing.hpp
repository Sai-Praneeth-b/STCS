// framing.hpp -- newline framing for the TCP byte stream (Phase 1, section 7.1).
//
// TCP delivers a stream of bytes, not messages. MessageFramer sits between
// recv() and the protocol layer: the caller feeds it whatever chunk recv()
// returned (1 byte, 4096 bytes, half a message, three messages...) and then
// pulls out complete frames one at a time.
//
// The framer touches no socket, uses no clock and never sleeps, so it can be
// unit-tested with arbitrary byte chunks (NFR-13).
//
// Rules implemented (Phase 1, 7.1 / 7.2):
//   * one message = bytes up to (not including) a '\n'
//   * a single trailing '\r' is stripped
//   * an empty line is reported as Blank (caller answers INVALID_MESSAGE)
//   * a line longer than the limit is reported ONCE as TooLarge and every
//     byte up to the next '\n' is discarded (resynchronisation)
//   * incomplete data stays buffered until its '\n' arrives
//   * the internal buffer never holds more than `max_line` bytes of
//     unterminated data, so a peer cannot make it grow without bound
#pragma once

#include <cstddef>
#include <string>

namespace stcs {

enum class FrameStatus {
    Message,   // `line` holds one complete line (without the delimiter)
    Blank,     // the peer sent an empty line
    TooLarge,  // the line exceeded the limit; its bytes are being discarded
};

struct Frame {
    FrameStatus status = FrameStatus::Message;
    std::string line;
};

class MessageFramer {
public:
    // `max_line` is the largest accepted line length in bytes, excluding the
    // '\n' delimiter. The server uses 8192 (requests); the client uses a
    // larger value for responses -- see protocol.hpp.
    explicit MessageFramer(std::size_t max_line);

    // Appends bytes exactly as returned by recv().
    void feed(const char* data, std::size_t length);

    // Extracts the next complete frame. Returns false when more bytes are
    // needed. Call repeatedly after every feed() until it returns false:
    // one recv() can complete several messages.
    bool next_frame(Frame& out);

    // Bytes currently held that do not yet form a complete line.
    std::size_t buffered_bytes() const { return buffer_.size(); }

    // True while the tail of an oversized line is being thrown away.
    bool discarding() const { return discarding_; }

private:
    std::size_t max_line_;
    std::string buffer_;
    bool discarding_ = false;
};

// Serialises one logical message: the JSON text plus exactly one '\n'.
// Throws std::logic_error if the text already contains a raw newline (the
// JSON serializer never produces one; this is a safety net).
std::string frame_message(const std::string& json_text);

}  // namespace stcs
