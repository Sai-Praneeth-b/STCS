// logger.cpp -- see logger.hpp.
#include "server/logger.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include "shared/timeutil.hpp"
#include "shared/utf8.hpp"

namespace stcs {

namespace {

const char* level_name(Logger::Level level) {
    switch (level) {
        case Logger::Level::Info:  return "INFO";
        case Logger::Level::Warn:  return "WARN";
        case Logger::Level::Error: return "ERROR";
    }
    return "INFO";
}

// Returns the directory part of "dir/file.log", or "" if there is none.
std::string parent_directory(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return "";
    }
    return path.substr(0, slash);
}

}  // namespace

bool Logger::open_file(const std::string& path, std::string& error) {
    const std::string directory = parent_directory(path);
    if (!directory.empty()) {
        // One level is enough for the default "logs/server.log".
        if (::mkdir(directory.c_str(), 0755) != 0 && errno != EEXIST) {
            error = "cannot create directory '" + directory + "': " + std::strerror(errno);
            return false;
        }
    }
    file_.open(path, std::ios::out | std::ios::app);
    if (!file_) {
        error = "cannot open log file '" + path + "' for writing";
        return false;
    }
    return true;
}

void Logger::write_line(Level level, const std::string& message) {
    // PHASE 3: take the logger mutex here so concurrent sessions cannot
    // interleave their lines.
    const std::string line = utc_timestamp_now() + " " + level_name(level) + " " + message;
    if (file_.is_open()) {
        file_ << line << '\n';
        file_.flush();
    }
    if (console_echo_) {
        std::cout << line << std::endl;  // std::endl flushes: lines appear immediately
    }
}

void Logger::info(const std::string& session_id, const std::string& message) {
    write_line(Level::Info, session_id + " " + message);
}

void Logger::warn(const std::string& session_id, const std::string& message) {
    write_line(Level::Warn, session_id + " " + message);
}

void Logger::error(const std::string& session_id, const std::string& message) {
    write_line(Level::Error, session_id + " " + message);
}

std::string Logger::sanitize(const std::string& text, std::size_t max_chars) {
    std::string out;
    std::size_t pos = 0;
    std::size_t count = 0;
    while (pos < text.size() && count < max_chars) {
        std::uint32_t code_point = 0;
        const std::size_t start = pos;
        if (!decode_utf8(text, pos, code_point)) {
            out.push_back('?');
            ++pos;
        } else if (is_control_code_point(code_point)) {
            out.push_back('?');
        } else {
            out.append(text, start, pos - start);
        }
        ++count;
    }
    if (pos < text.size()) {
        out += "...";
    }
    return out;
}

}  // namespace stcs
