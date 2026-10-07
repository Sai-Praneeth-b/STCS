// input_parser.cpp -- see input_parser.hpp.
#include "client/input_parser.hpp"

#include <cctype>
#include <vector>

#include "shared/protocol.hpp"
#include "shared/validation.hpp"

namespace stcs {

namespace {

std::string to_lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string to_upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// Splits "verb rest of line" into the verb and the (trimmed) remainder.
void split_verb(const std::string& line, std::string& verb, std::string& rest) {
    const std::size_t space = line.find_first_of(" \t");
    if (space == std::string::npos) {
        verb = line;
        rest.clear();
    } else {
        verb = line.substr(0, space);
        rest = trim_ascii(line.substr(space + 1));
    }
}

UserCommand invalid(const std::string& message) {
    UserCommand command;
    command.kind = UserCommandKind::Invalid;
    command.error = message;
    return command;
}

// claim/release/done/delete: the id is optional here (the app prompts for a
// missing one) but must be valid if present.
UserCommand parse_task_id_command(UserCommandKind kind, const std::string& verb,
                                  const std::string& rest) {
    UserCommand command;
    command.kind = kind;
    if (rest.empty()) {
        return command;  // the application will prompt for the id
    }
    if (rest.find_first_of(" \t") != std::string::npos) {
        return invalid("Usage: " + verb + " <task id>   (one whole number, e.g. '" + verb + " 3').");
    }
    long long id = 0;
    std::string reason;
    if (!parse_task_id_text(rest, id, reason)) {
        return invalid("Task id '" + rest + "' " + reason + ".");
    }
    command.task_id = id;
    return command;
}

}  // namespace

UserCommand parse_user_command(const std::string& raw_line) {
    const std::string line = trim_ascii(raw_line);
    UserCommand command;
    if (line.empty()) {
        command.kind = UserCommandKind::Empty;
        return command;
    }

    std::string verb_text;
    std::string rest;
    split_verb(line, verb_text, rest);
    const std::string verb = to_lower(verb_text);

    if (verb == "help" || verb == "?") {
        if (rest.empty()) {
            command.kind = UserCommandKind::Help;
            return command;
        }
        std::string subverb;
        std::string topic;
        split_verb(rest, subverb, topic);
        if (to_lower(subverb) != "server") {
            return invalid("Usage: help   or   help server [COMMAND]");
        }
        command.kind = UserCommandKind::HelpServer;
        if (!topic.empty()) {
            Command ignored = Command::Help;
            const std::string upper = to_upper(topic);
            if (!parse_command(upper, ignored)) {
                return invalid("'" + topic + "' is not a protocol command. Try 'help server'.");
            }
            command.help_topic = upper;
        }
        return command;
    }

    if (verb == "add") {
        command.kind = UserCommandKind::Add;
        command.title = rest;  // may be empty: the application prompts for it
        return command;
    }

    if (verb == "list") {
        command.kind = UserCommandKind::List;
        if (!rest.empty()) {
            const std::string upper = to_upper(rest);
            if (upper != "OPEN" && upper != "CLAIMED" && upper != "DONE") {
                return invalid("Status filter must be open, claimed or done (or leave it out).");
            }
            command.status = upper;
        }
        return command;
    }

    if (verb == "claim")   return parse_task_id_command(UserCommandKind::Claim, "claim", rest);
    if (verb == "release") return parse_task_id_command(UserCommandKind::Release, "release", rest);
    if (verb == "done" || verb == "complete") {
        return parse_task_id_command(UserCommandKind::Done, "done", rest);
    }
    if (verb == "delete")  return parse_task_id_command(UserCommandKind::Delete, "delete", rest);

    if (verb == "quit" || verb == "exit") {
        command.kind = UserCommandKind::Quit;
        return command;
    }

    if (verb == "raw") {
        if (rest.empty()) {
            return invalid("Usage: raw <COMMAND> [payload as JSON object]");
        }
        std::string name;
        std::string payload_text;
        split_verb(rest, name, payload_text);
        command.kind = UserCommandKind::Raw;
        command.raw_command = name;
        command.raw_payload_text = payload_text;
        return command;
    }

    return invalid("Unknown command '" + verb_text + "'. Type 'help' to see the commands.");
}

}  // namespace stcs
