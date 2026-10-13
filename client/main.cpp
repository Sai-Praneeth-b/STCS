#pragma once

#include <optional>
#include <string>

namespace stcs {

enum class UserCommandKind {
    Empty,
    Help,
    HelpServer,
    Add,
    List,
    Claim,
    Release,
    Done,
    Delete,
    Quit,
    Raw,
    Invalid,
};

struct UserCommand {
    UserCommandKind kind = UserCommandKind::Empty;

    std::string title;
    std::optional<std::string> status;
    std::optional<long long> task_id;
    std::optional<std::string> help_topic;
    std::string raw_command;
    std::string raw_payload_text;
    std::string error;
};

UserCommand parse_user_command(const std::string& line);

}
