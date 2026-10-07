// test_framing.cpp -- unit tests for the framing layer (no sockets, no sleeps).
//
// These tests choose the exact chunk boundaries themselves, so they prove the
// framer's behaviour for every way TCP could deliver the bytes (Phase 1,
// T-13 and T-14, unit part).
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "shared/framing.hpp"
#include "shared/json.hpp"
#include "shared/protocol.hpp"
#include "tests/test_support.hpp"

using namespace stcs;

namespace {

// A realistic request containing multi-byte UTF-8 (so byte splits can land
// inside a character).
std::string sample_request(const std::string& id, const std::string& title) {
    Json payload = Json::object();
    payload.set("title", Json::text(title));
    payload.set("priority", Json::text("HIGH"));
    return frame_message(make_request(id, "CREATE_TASK", payload).dump());
}

std::vector<Frame> drain(MessageFramer& framer) {
    std::vector<Frame> frames;
    Frame frame;
    while (framer.next_frame(frame)) frames.push_back(frame);
    return frames;
}

}  // namespace

int main() {
    testing::run("T-13a", "one message fed one byte at a time: nothing until the newline", [] {
        const std::string wire = sample_request("r1", "Caf\xC3\xA9 r\xC3\xA9sum\xC3\xA9 \xE2\x9C\x93");
        MessageFramer framer(kMaxMessageBytes);
        std::size_t messages = 0;
        for (std::size_t i = 0; i < wire.size(); ++i) {
            framer.feed(&wire[i], 1);
            const std::vector<Frame> frames = drain(framer);
            if (i + 1 < wire.size()) {
                CHECK_EQ(frames.size(), 0u);  // no newline yet: no message may appear
            } else {
                CHECK_EQ(frames.size(), 1u);
                CHECK(frames[0].status == FrameStatus::Message);
                CHECK_EQ(frames[0].line + "\n", wire);
                ++messages;
            }
        }
        CHECK_EQ(messages, 1u);
        CHECK_EQ(framer.buffered_bytes(), 0u);
    });

    testing::run("T-13b", "message split in three pieces (the Phase 1 example split)", [] {
        const std::string p1 = "{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"r1\",";
        const std::string p2 = "\"command\":\"CREATE_TASK\",\"payload\":{\"title\":\"Test\",\"priority\":\"HIGH\"}}";
        MessageFramer framer(kMaxMessageBytes);
        framer.feed(p1.data(), p1.size());
        CHECK_EQ(drain(framer).size(), 0u);
        framer.feed(p2.data(), p2.size());
        CHECK_EQ(drain(framer).size(), 0u);  // complete JSON but still no newline
        framer.feed("\n", 1);
        const std::vector<Frame> frames = drain(framer);
        CHECK_EQ(frames.size(), 1u);
        CHECK_EQ(frames[0].line, p1 + p2);
    });

    testing::run("T-14a", "two complete messages in one feed are both returned, in order", [] {
        const std::string a = sample_request("r1", "A");
        const std::string b = sample_request("r2", "B");
        const std::string both = a + b;
        MessageFramer framer(kMaxMessageBytes);
        framer.feed(both.data(), both.size());
        const std::vector<Frame> frames = drain(framer);
        CHECK_EQ(frames.size(), 2u);
        CHECK_EQ(frames[0].line + "\n", a);
        CHECK_EQ(frames[1].line + "\n", b);
        CHECK_EQ(framer.buffered_bytes(), 0u);
    });

    testing::run("T-14b", "two messages plus a trailing fragment: fragment stays buffered", [] {
        const std::string a = sample_request("r1", "A");
        const std::string b = sample_request("r2", "B");
        const std::string c = sample_request("r3", "C");
        const std::string fragment = c.substr(0, c.size() - 7);
        const std::string rest = c.substr(c.size() - 7);
        const std::string chunk = a + b + fragment;

        MessageFramer framer(kMaxMessageBytes);
        framer.feed(chunk.data(), chunk.size());
        std::vector<Frame> frames = drain(framer);
        CHECK_EQ(frames.size(), 2u);                       // C is NOT delivered yet
        CHECK_EQ(framer.buffered_bytes(), fragment.size()); // and nothing was lost
        framer.feed(rest.data(), rest.size());
        frames = drain(framer);
        CHECK_EQ(frames.size(), 1u);
        CHECK_EQ(frames[0].line + "\n", c);
    });

    testing::run("U-F1", "CRLF line endings: the trailing CR is stripped", [] {
        MessageFramer framer(kMaxMessageBytes);
        const std::string wire = "{\"a\":1}\r\n";
        framer.feed(wire.data(), wire.size());
        const std::vector<Frame> frames = drain(framer);
        CHECK_EQ(frames.size(), 1u);
        CHECK_EQ(frames[0].line, std::string("{\"a\":1}"));
    });

    testing::run("U-F2", "blank lines are reported as Blank (session stays usable)", [] {
        MessageFramer framer(kMaxMessageBytes);
        const std::string wire = "\n\r\n{\"a\":1}\n";
        framer.feed(wire.data(), wire.size());
        const std::vector<Frame> frames = drain(framer);
        CHECK_EQ(frames.size(), 3u);
        CHECK(frames[0].status == FrameStatus::Blank);
        CHECK(frames[1].status == FrameStatus::Blank);
        CHECK(frames[2].status == FrameStatus::Message);
    });

    testing::run("U-F3", "limit boundary: 8192 bytes accepted, 8193 rejected as TooLarge", [] {
        MessageFramer framer(kMaxMessageBytes);
        const std::string ok(kMaxMessageBytes, 'x');
        const std::string wire_ok = ok + "\n";
        framer.feed(wire_ok.data(), wire_ok.size());
        std::vector<Frame> frames = drain(framer);
        CHECK_EQ(frames.size(), 1u);
        CHECK(frames[0].status == FrameStatus::Message);

        const std::string too_big(kMaxMessageBytes + 1, 'x');
        const std::string wire_big = too_big + "\n";
        framer.feed(wire_big.data(), wire_big.size());
        frames = drain(framer);
        CHECK_EQ(frames.size(), 1u);
        CHECK(frames[0].status == FrameStatus::TooLarge);
        CHECK_EQ(framer.buffered_bytes(), 0u);
    });

    testing::run("U-F4", "8192 bytes arriving without a newline are held, not rejected", [] {
        MessageFramer framer(kMaxMessageBytes);
        const std::string exactly(kMaxMessageBytes, 'x');
        framer.feed(exactly.data(), exactly.size());
        CHECK_EQ(drain(framer).size(), 0u);
        framer.feed("\n", 1);
        const std::vector<Frame> frames = drain(framer);
        CHECK_EQ(frames.size(), 1u);
        CHECK(frames[0].status == FrameStatus::Message);
    });

    testing::run("U-F5", "oversize line in 4096-byte chunks: one error, bounded memory, resync", [] {
        MessageFramer framer(kMaxMessageBytes);
        const std::string chunk(kReceiveChunkBytes, 'y');
        std::size_t too_large_events = 0;
        std::size_t largest_buffer = 0;
        for (int i = 0; i < 12; ++i) {  // 48 KiB with no newline
            framer.feed(chunk.data(), chunk.size());
            for (const Frame& frame : drain(framer)) {
                CHECK(frame.status == FrameStatus::TooLarge);
                ++too_large_events;
            }
            largest_buffer = std::max(largest_buffer, framer.buffered_bytes());
        }
        CHECK_EQ(too_large_events, 1u);                 // reported once, not per chunk
        CHECK(largest_buffer <= kMaxMessageBytes);      // memory never grew without bound
        CHECK(framer.discarding());

        // The rest of the bad line, its newline, then a good message.
        const std::string tail = "zzz\n" + sample_request("r9", "after");
        framer.feed(tail.data(), tail.size());
        const std::vector<Frame> frames = drain(framer);
        CHECK_EQ(frames.size(), 1u);
        CHECK(frames[0].status == FrameStatus::Message);  // stream resynchronised
        CHECK(!framer.discarding());
    });

    testing::run("U-F6", "valid message, oversize line, valid message in one feed", [] {
        const std::string good1 = sample_request("r1", "one");
        const std::string good2 = sample_request("r2", "two");
        const std::string wire = good1 + std::string(20000, 'q') + "\n" + good2;
        MessageFramer framer(kMaxMessageBytes);
        framer.feed(wire.data(), wire.size());
        const std::vector<Frame> frames = drain(framer);
        CHECK_EQ(frames.size(), 3u);
        CHECK(frames[0].status == FrameStatus::Message);
        CHECK(frames[1].status == FrameStatus::TooLarge);
        CHECK(frames[2].status == FrameStatus::Message);
        CHECK_EQ(frames[2].line + "\n", good2);
    });

    testing::run("U-F7", "any chunking of a message stream yields the same messages", [] {
        std::vector<std::string> expected;
        std::string stream;
        for (int i = 0; i < 25; ++i) {
            const std::string wire = sample_request("r" + std::to_string(i),
                                                    "T\xC3\xA9st " + std::to_string(i));
            expected.push_back(wire.substr(0, wire.size() - 1));
            stream += wire;
        }
        unsigned seed = 12345;  // deterministic pseudo-random chunk sizes
        for (int trial = 0; trial < 200; ++trial) {
            MessageFramer framer(kMaxMessageBytes);
            std::vector<std::string> got;
            std::size_t pos = 0;
            while (pos < stream.size()) {
                seed = seed * 1103515245u + 12345u;
                const std::size_t size = 1 + (seed >> 16) % 300;
                const std::size_t take = std::min(size, stream.size() - pos);
                framer.feed(stream.data() + pos, take);
                pos += take;
                for (const Frame& frame : drain(framer)) {
                    CHECK(frame.status == FrameStatus::Message);
                    got.push_back(frame.line);
                }
            }
            CHECK(got == expected);
            CHECK_EQ(framer.buffered_bytes(), 0u);
        }
    });

    testing::run("U-F8", "serialised messages never contain a raw newline", [] {
        Json payload = Json::object();
        payload.set("title", Json::text("line one\nline two\r\ttabbed \"quoted\""));
        const std::string text = make_request("r1", "CREATE_TASK", payload).dump();
        CHECK(text.find('\n') == std::string::npos);
        CHECK(text.find('\r') == std::string::npos);
        CHECK_EQ(frame_message(text).size(), text.size() + 1);

        bool threw = false;
        try {
            frame_message("bad\nline");
        } catch (const std::logic_error&) {
            threw = true;
        }
        CHECK(threw);
    });

    return testing::summarize("test_framing");
}
