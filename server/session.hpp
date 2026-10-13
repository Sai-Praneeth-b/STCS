#pragma once

#include <map>
#include <string>

#include "shared/framing.hpp"
#include "shared/protocol.hpp"

namespace stcs {

class ClientSession {
public:
    ClientSession(std::string session_id, std::string peer_address);

    const std::string& id() const { return session_id_; }
    const std::string& peer_address() const { return peer_address_; }
    const std::string& display_name() const { return display_name_; }
    SessionState state() const { return state_; }
    MessageFramer& framer() { return framer_; }

    void mark_ready(const std::string& display_name);
    void begin_closing();
    void mark_closed();

private:
    std::string session_id_;
    std::string peer_address_;
    std::string display_name_;
    SessionState state_ = SessionState::Connected;
    MessageFramer framer_;
};

class SessionRegistry {
public:
    std::string next_session_id();

    void add(const std::string& session_id);

    bool reserve_name(const std::string& session_id, const std::string& display_name);

    void remove(const std::string& session_id);

    std::size_t active_count() const { return names_by_session_.size(); }

private:
    unsigned next_number_ = 1;
    std::map<std::string, std::string> names_by_session_;
};

}
