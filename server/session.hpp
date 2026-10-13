// session.hpp -- per-connection session state and the session registry.
//
// A ClientSession is everything the server remembers about one connection
// (Phase 1, section 8, "ClientSession"): its ID, display name, protocol
// state, peer address and receive buffer. Only the code that serves that
// connection ever touches it, which is why the Phase 3 design needs no lock
// around it.
//
// The SessionRegistry knows which display names are in use among active
// sessions. With one client in Phase 2 it can never report a clash, but the
// HELLO handler already goes through it so Phase 3 only has to add the
// session_lock.
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

    // State transitions (Phase 1, 7.4).
    void mark_ready(const std::string& display_name);  // CONNECTED -> READY
    void begin_closing();                              // any -> CLOSING
    void mark_closed();                                // CLOSING -> CLOSED

private:
    std::string session_id_;
    std::string peer_address_;
    std::string display_name_;
    SessionState state_ = SessionState::Connected;
    MessageFramer framer_;
};

class SessionRegistry {
public:
    // Creates the next unique session ID ("sess-0001", "sess-0002", ...).
    std::string next_session_id();

    // Registers a session that has not chosen a name yet.
    void add(const std::string& session_id);

    // Reserves `display_name` for the session. Returns false if another
    // active session already holds it. Names are compared exactly.
    bool reserve_name(const std::string& session_id, const std::string& display_name);

    // Removes the session and frees its name (graceful or abrupt end alike).
    void remove(const std::string& session_id);

    std::size_t active_count() const { return names_by_session_.size(); }

private:
    unsigned next_number_ = 1;
    std::map<std::string, std::string> names_by_session_;  // "" = no name yet
};

}  // namespace stcs
