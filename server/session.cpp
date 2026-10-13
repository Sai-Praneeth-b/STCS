// session.cpp -- see session.hpp.
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

std::string SessionRegistry::next_session_id() {
    // PHASE 3: guard next_number_ with session_lock.
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "sess-%04x", next_number_++);
    return buffer;
}

void SessionRegistry::add(const std::string& session_id) {
    names_by_session_[session_id] = "";
}

bool SessionRegistry::reserve_name(const std::string& session_id,
                                   const std::string& display_name) {
    for (const auto& entry : names_by_session_) {
        if (entry.first != session_id && entry.second == display_name) {
            return false;
        }
    }
    names_by_session_[session_id] = display_name;
    return true;
}

void SessionRegistry::remove(const std::string& session_id) {
    names_by_session_.erase(session_id);
}

}  // namespace stcs
