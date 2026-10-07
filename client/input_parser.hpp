// input_parser.hpp -- turns one line typed by the user into a UserCommand.
//
// This is the client's input-validation layer (FR-25, NFR-07). Everything
// that can be checked without the server is checked here, so obviously bad
// input never produces a network request. The server still re-validates
// everything it receives.
#pragma once

#include <optional>
#include <string>

namespace stcs {

enum class UserCommandKind {
    Empty,       // blank line: just prompt again
    Help,        // local help (no server needed)
    HelpServer,  // HELP request to the server
    Add,
    List,
    Claim,
    Release,
    Done,
    Delete,
    Quit,
    Raw,         // protocol testing: send an arbitrary command name
    Invalid,     // unusable input; `error` explains why
};

struct UserCommand {
    UserCommandKind kind = UserCommandKind::Empty;

    std::string title;                      // add <title>
    std::optional<std::string> status;      // list <status>  (OPEN/CLAIMED/DONE)
    std::optional<long long> task_id;       // claim/release/done/delete <id>
    std::optional<std::string> help_topic;  // help server <COMMAND>
    std::string raw_command;                // raw <COMMAND> ...
    std::string raw_payload_text;           // raw <COMMAND> <json>
    std::string error;                      // for Invalid
};

UserCommand parse_user_command(const std::string& line);

}  // namespace stcs
