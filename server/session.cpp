#include "server/session.hpp"

#include <cstdio>

namespace stcs {

ClientSession::ClientSession(std::string session_id, std::string peer_address)
    : session_id_(std::move(session_id)),
      peer_address_(std::move(peer_address)),
      framer_(kMaxMessageBytes) {}

void ClientSession::mark_ready(const std::string& display_name) {
    display_name_ = display_name;
    state_ = SessionState::Ready;
}

void ClientSession::begin_closing() {
    state_ = SessionState::Closing;
}

void ClientSession::mark_closed() {
    state_ = SessionState::Closed;
}

std::size_t SessionRegistry::active_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return names_by_session_.size();
}

std::string SessionRegistry::next_session_id() {
    std::lock_guard<std::mutex> lock(mutex_);
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "sess-%04x", next_number_++);
    return buffer;
}

void SessionRegistry::add(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    names_by_session_[session_id] = "";
}

bool SessionRegistry::reserve_name(const std::string& session_id,
                                   const std::string& display_name) {
    // The duplicate-name check and the reservation happen under one lock, so two
    // clients sending HELLO with the same name at once cannot both succeed.
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : names_by_session_) {
        if (entry.first != session_id && entry.second == display_name) {
            return false;
        }
    }
    names_by_session_[session_id] = display_name;
    return true;
}

void SessionRegistry::remove(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    names_by_session_.erase(session_id);
}

}
