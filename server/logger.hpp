#pragma once

#include <fstream>
#include <string>

namespace stcs {

class Logger {
public:
    enum class Level { Info, Warn, Error };

    bool open_file(const std::string& path, std::string& error);

    void set_console_echo(bool enabled) { console_echo_ = enabled; }

    void info(const std::string& message) { write_line(Level::Info, message); }
    void warn(const std::string& message) { write_line(Level::Warn, message); }
    void error(const std::string& message) { write_line(Level::Error, message); }

    void info(const std::string& session_id, const std::string& message);
    void warn(const std::string& session_id, const std::string& message);
    void error(const std::string& session_id, const std::string& message);

    static std::string sanitize(const std::string& text, std::size_t max_chars = 60);

private:
    void write_line(Level level, const std::string& message);

    std::ofstream file_;
    bool console_echo_ = true;
};

}
