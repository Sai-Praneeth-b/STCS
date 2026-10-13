// logger.hpp -- timestamped server log (Phase 1 FR-22, Phase 2 section 6.3).
//
// Every line has the form
//     2026-10-04T01:22:07Z INFO sess-0001 CREATE_TASK success task_id=1
// i.e. ISO-8601 UTC timestamp, level, then free text. Lines go to the log
// file (flushed after every line, so a crash loses nothing) and are echoed to
// standard output so the instructor sees them in the server terminal.
//
// The logger is the only writer of the log file. In Phase 3 the single
// `write_line` function is where the "logger serialises writes" mutex from
// the Phase 1 design will be taken; nothing else needs to change.
#pragma once

#include <fstream>
#include <string>

namespace stcs {

class Logger {
public:
    enum class Level { Info, Warn, Error };

    // Opens `path` for appending, creating its parent directory (one level)
    // if needed. Returns false and fills `error` on failure; the logger then
    // keeps working with console output only.
    bool open_file(const std::string& path, std::string& error);

    // Turn the stdout echo on or off (tests use it to keep output quiet).
    void set_console_echo(bool enabled) { console_echo_ = enabled; }

    void info(const std::string& message) { write_line(Level::Info, message); }
    void warn(const std::string& message) { write_line(Level::Warn, message); }
    void error(const std::string& message) { write_line(Level::Error, message); }

    // Same, with the "sess-NNNN " prefix used for per-session lines.
    void info(const std::string& session_id, const std::string& message);
    void warn(const std::string& session_id, const std::string& message);
    void error(const std::string& session_id, const std::string& message);

    // Makes client-supplied text safe for a single log line: control
    // characters become '?', and the result is cut to `max_chars`.
    static std::string sanitize(const std::string& text, std::size_t max_chars = 60);

private:
    void write_line(Level level, const std::string& message);

    std::ofstream file_;
    bool console_echo_ = true;
};

}  // namespace stcs
