// test_units.cpp -- unit and in-process protocol tests (no sockets).
//
// Covers: JSON, field validation, envelope validation, TaskManager,
// SessionRegistry, the request dispatcher (driven with Frames, exactly as the
// server loop does), and the client's display and input-parsing code.
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "client/display.hpp"
#include "client/input_parser.hpp"
#include "server/dispatcher.hpp"
#include "server/logger.hpp"
#include "server/session.hpp"
#include "server/task_manager.hpp"
#include "shared/framing.hpp"
#include "shared/json.hpp"
#include "shared/protocol.hpp"
#include "shared/utf8.hpp"
#include "shared/validation.hpp"
#include "tests/test_support.hpp"

using namespace stcs;

namespace {

// ---- helpers for driving the dispatcher in-process -------------------------

struct Rig {
    Logger logger;
    TaskManager tasks{[] { return std::string("2026-01-01T00:00:00Z"); }};
    SessionRegistry sessions;
    RequestDispatcher dispatcher{tasks, sessions, logger};
    ClientSession session{sessions.next_session_id(), "127.0.0.1:1"};

    Rig() {
        logger.set_console_echo(false);
        sessions.add(session.id());
    }

    // Sends one request line and returns the parsed response.
    Json send(const std::string& line) {
        Frame frame;
        frame.status = FrameStatus::Message;
        frame.line = line;
        return parse(dispatcher.handle_frame(session, frame));
    }
    Json send_frame(FrameStatus status) {
        Frame frame;
        frame.status = status;
        return parse(dispatcher.handle_frame(session, frame));
    }
    static Json parse(const std::string& framed) {
        CHECK(!framed.empty() && framed.back() == '\n');
        Json json;
        std::string error;
        CHECK(Json::parse(framed.substr(0, framed.size() - 1), json, error));
        return json;
    }
    Json request(const std::string& id, const std::string& command, const std::string& payload) {
        return send("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"" + id +
                    "\",\"command\":\"" + command + "\",\"payload\":" + payload + "}");
    }
    void hello(const std::string& name = "Ana") {
        const Json r = request("h", "HELLO", "{\"display_name\":\"" + name + "\"}");
        CHECK_EQ(status_of(r), std::string("OK"));
    }

    static std::string status_of(const Json& r) { return r.find("status")->string_value(); }
    static std::string code_of(const Json& r) { return r.find("error_code")->string_value(); }
};

std::string error_code(const Json& response) {
    return response.find("status")->string_value() == "ERROR" ? Rig::code_of(response) : "";
}

Json parse_json(const std::string& text) {
    Json json;
    std::string error;
    if (!Json::parse(text, json, error)) throw testing::Failure("parse failed: " + error);
    return json;
}

bool json_fails(const std::string& text) {
    Json json;
    std::string error;
    return !Json::parse(text, json, error);
}

// A task with every field set, for display tests.
Task make_task(long long id, const std::string& title, const std::string& owner = "") {
    Task task;
    task.task_id = id;
    task.title = title;
    task.priority = Priority::High;
    task.status = owner.empty() ? TaskStatus::Open : TaskStatus::Claimed;
    if (!owner.empty()) task.claimed_by = owner;
    task.created_by = "Ana";
    task.created_at = "2026-10-04T01:22:07Z";
    return task;
}

std::size_t widest_line(const std::string& text) {
    std::size_t widest = 0, start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        widest = std::max(widest, utf8_length(text.substr(start, end - start)));
        start = end + 1;
    }
    return widest;
}

// A TaskManager whose create_task fails, to exercise SERVER_ERROR.
struct FailingTaskManager : TaskManager {
    Task create_task(const std::string&, Priority, const std::string&) override {
        throw std::runtime_error("simulated storage failure at /secret/path.cpp:42");
    }
};

}  // namespace

int main() {
    // ----------------------------------------------------------------- JSON
    testing::run("U-J1", "JSON round trip keeps key order and escapes control characters", [] {
        const std::string text = "{\"b\":1,\"a\":[true,false,null,\"x\\n\\u00e9\"],\"c\":{\"d\":-5}}";
        const Json json = parse_json(text);
        CHECK_EQ(json.dump(), std::string("{\"b\":1,\"a\":[true,false,null,\"x\\n\xC3\xA9\"],\"c\":{\"d\":-5}}"));
    });
    testing::run("U-J2", "JSON: surrogate pair decodes to one 4-byte UTF-8 character", [] {
        const Json json = parse_json("\"\\ud83d\\ude00\"");
        CHECK_EQ(json.string_value(), std::string("\xF0\x9F\x98\x80"));
    });
    testing::run("U-J3", "JSON: malformed documents are rejected", [] {
        CHECK(json_fails(""));
        CHECK(json_fails("{"));
        CHECK(json_fails("{\"a\":1,}"));
        CHECK(json_fails("[1,]"));
        CHECK(json_fails("{\"a\":1} extra"));
        CHECK(json_fails("{'a':1}"));
        CHECK(json_fails("{\"a\":01}"));
        CHECK(json_fails("{\"a\":1,\"a\":2}"));            // duplicate key
        CHECK(json_fails("\"\\ud83d\""));                   // lone surrogate
        CHECK(json_fails("\"bad\xFF\""));                   // invalid UTF-8
        CHECK(json_fails("\"raw\ttab\""));                  // raw control char
        CHECK(json_fails(std::string(40, '[') + std::string(40, ']')));  // too deep
    });
    testing::run("U-J4", "JSON: integers vs non-integers are distinguished", [] {
        CHECK(parse_json("7").is_int());
        CHECK(!parse_json("7.0").is_int());
        CHECK(!parse_json("1e2").is_int());
        CHECK(!parse_json("99999999999999999999").is_int());  // overflows 64 bits
    });

    // ----------------------------------------------------------- validation
    testing::run("U-V1", "display name rules (1-24, letters digits space _ - .)", [] {
        std::string why;
        CHECK(validate_display_name("Ana", why));
        CHECK(validate_display_name("a_b-c.D 9", why));
        CHECK(validate_display_name(std::string(24, 'a'), why));
        CHECK(!validate_display_name("", why));
        CHECK(!validate_display_name(std::string(25, 'a'), why));
        CHECK(!validate_display_name("Ana!", why));
        CHECK(!validate_display_name("Ren\xC3\xA9", why));
        CHECK(!validate_display_name(" Ana", why));
        CHECK(!validate_display_name("Ana ", why));
        CHECK(!validate_display_name("   ", why));
    });
    testing::run("U-V2", "title rules: trimmed, 1-120 chars, not blank, no control chars", [] {
        std::string out, why;
        CHECK(validate_title("  Write intro  ", out, why));
        CHECK_EQ(out, std::string("Write intro"));
        CHECK(validate_title(std::string(120, 'x'), out, why));
        CHECK(!validate_title(std::string(121, 'x'), out, why));
        CHECK(!validate_title("   \t ", out, why));
        CHECK(!validate_title("", out, why));
        CHECK(!validate_title("a\x01" "b", out, why));
        CHECK(!validate_title("a\x7F" "b", out, why));
        // 120 multi-byte characters is still 120 characters (not 240 bytes).
        std::string accents;
        for (int i = 0; i < 120; ++i) accents += "\xC3\xA9";
        CHECK(validate_title(accents, out, why));
    });
    testing::run("U-V3", "port and task-id text parsing", [] {
        std::string why;
        int port = 0;
        long long id = 0;
        CHECK(parse_port_text("5050", port, why) && port == 5050);
        CHECK(parse_port_text("1", port, why));
        CHECK(parse_port_text("65535", port, why));
        CHECK(!parse_port_text("0", port, why));
        CHECK(!parse_port_text("65536", port, why));
        CHECK(!parse_port_text("70000", port, why));
        CHECK(!parse_port_text("abc", port, why));
        CHECK(!parse_port_text("-1", port, why));
        CHECK(!parse_port_text("", port, why));
        CHECK(!parse_port_text("50 50", port, why));
        CHECK(!parse_port_text("99999999999999999999", port, why));
        CHECK(parse_task_id_text("12", id, why) && id == 12);
        CHECK(!parse_task_id_text("0", id, why));
        CHECK(!parse_task_id_text("-3", id, why));
        CHECK(!parse_task_id_text("abc", id, why));
        CHECK(!parse_task_id_text("1.5", id, why));
        CHECK(!parse_task_id_text("99999999999999999999", id, why));
    });

    // ------------------------------------------------------------- envelope
    testing::run("U-E1", "envelope: errors name the right code and recover request_id", [] {
        Request request;
        EnvelopeFailure failure;
        CHECK(!parse_request_line("not json", request, failure));
        CHECK(failure.error.code == ErrorCode::InvalidMessage);
        CHECK(!failure.request_id.has_value());

        CHECK(!parse_request_line("[1,2]", request, failure));
        CHECK(failure.error.code == ErrorCode::InvalidMessage);

        CHECK(!parse_request_line("{\"type\":\"REQUEST\",\"request_id\":\"q1\",\"command\":\"QUIT\",\"payload\":{}}", request, failure));
        CHECK(failure.error.code == ErrorCode::MissingField);
        CHECK_EQ(failure.error.payload.find("field")->string_value(), std::string("version"));
        CHECK_EQ(*failure.request_id, std::string("q1"));     // echoed: it was recoverable
        CHECK_EQ(*failure.command_text, std::string("QUIT"));

        CHECK(!parse_request_line("{\"version\":2,\"type\":\"REQUEST\",\"request_id\":\"q\",\"command\":\"QUIT\",\"payload\":{}}", request, failure));
        CHECK(failure.error.code == ErrorCode::InvalidVersion);
        CHECK(failure.error.payload.find("supported_versions") != nullptr);

        CHECK(!parse_request_line("{\"version\":\"1\",\"type\":\"REQUEST\",\"request_id\":\"q\",\"command\":\"QUIT\",\"payload\":{}}", request, failure));
        CHECK(failure.error.code == ErrorCode::InvalidField);

        CHECK(!parse_request_line("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"q\",\"command\":\"QUIT\"}", request, failure));
        CHECK(failure.error.code == ErrorCode::MissingField);

        CHECK(!parse_request_line("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"q\",\"command\":\"QUIT\",\"payload\":[]}", request, failure));
        CHECK(failure.error.code == ErrorCode::InvalidField);

        CHECK(!parse_request_line("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"q\",\"command\":\"MAKE_COFFEE\",\"payload\":{}}", request, failure));
        CHECK(failure.error.code == ErrorCode::UnknownCommand);

        CHECK(!parse_request_line("{\"version\":1,\"type\":\"RESPONSE\",\"request_id\":\"q\",\"command\":\"QUIT\",\"payload\":{}}", request, failure));
        CHECK(failure.error.code == ErrorCode::InvalidField);

        CHECK(parse_request_line("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"q\",\"command\":\"QUIT\",\"payload\":{},\"extra\":true}", request, failure));
        CHECK(request.command == Command::Quit);   // unknown extra fields are ignored
    });
    testing::run("U-E2", "invalid UTF-8 on the wire is INVALID_MESSAGE", [] {
        Request request;
        EnvelopeFailure failure;
        CHECK(!parse_request_line("{\"version\":1,\"x\":\"\xC3\x28\"}", request, failure));
        CHECK(failure.error.code == ErrorCode::InvalidMessage);
    });

    // --------------------------------------------------------- task manager
    testing::run("U-T1", "task manager: create assigns ids, OPEN, null owner, ascending list", [] {
        TaskManager tasks([] { return std::string("2026-01-01T00:00:00Z"); });
        const Task a = tasks.create_task("A", Priority::High, "Ana");
        const Task b = tasks.create_task("B", Priority::Medium, "Ana");
        CHECK_EQ(a.task_id, 1);
        CHECK_EQ(b.task_id, 2);
        CHECK(a.status == TaskStatus::Open);
        CHECK(!a.claimed_by.has_value());
        CHECK_EQ(a.created_at, std::string("2026-01-01T00:00:00Z"));
        const auto all = tasks.list_tasks(std::nullopt);
        CHECK_EQ(all.size(), 2u);
        CHECK(all[0].task_id < all[1].task_id);
    });
    testing::run("U-T2", "task manager: legal and illegal status transitions", [] {
        TaskManager tasks;
        const long long id = tasks.create_task("A", Priority::Low, "Ana").task_id;

        CHECK(tasks.release_task(id).outcome == TaskOutcome::Conflict);   // OPEN: not claimed
        CHECK(tasks.claim_task(id, "Ben").outcome == TaskOutcome::Ok);
        const TaskResult again = tasks.claim_task(id, "Cara");           // CLAIMED: conflict
        CHECK(again.outcome == TaskOutcome::Conflict);
        CHECK_EQ(*again.task.claimed_by, std::string("Ben"));            // owner unchanged
        CHECK(tasks.release_task(id).outcome == TaskOutcome::Ok);
        CHECK(tasks.claim_task(id, "Ana").outcome == TaskOutcome::Ok);
        const TaskResult done = tasks.complete_task(id);
        CHECK(done.outcome == TaskOutcome::Ok);
        CHECK(done.task.status == TaskStatus::Done);
        CHECK_EQ(*done.task.claimed_by, std::string("Ana"));             // kept when DONE
        CHECK(tasks.complete_task(id).outcome == TaskOutcome::Conflict);
        CHECK(tasks.claim_task(id, "Ben").outcome == TaskOutcome::Conflict);
        CHECK(tasks.release_task(id).outcome == TaskOutcome::Conflict);
    });
    testing::run("U-T3", "task manager: complete directly from OPEN; ids are never reused", [] {
        TaskManager tasks;
        const long long first = tasks.create_task("A", Priority::Low, "Ana").task_id;
        CHECK(tasks.complete_task(first).outcome == TaskOutcome::Ok);
        const long long second = tasks.create_task("B", Priority::Low, "Ana").task_id;
        CHECK(tasks.delete_task(second).outcome == TaskOutcome::Ok);
        const long long third = tasks.create_task("C", Priority::Low, "Ana").task_id;
        CHECK(third > second);                                  // 2 is not handed out again
        CHECK(tasks.delete_task(second).outcome == TaskOutcome::NotFound);
        CHECK(tasks.claim_task(9999, "Ana").outcome == TaskOutcome::NotFound);
    });
    testing::run("T-03u", "task manager: status filter returns only matching tasks", [] {
        TaskManager tasks;
        tasks.create_task("open", Priority::Low, "Ana");
        const long long claimed = tasks.create_task("claimed", Priority::Low, "Ana").task_id;
        const long long done = tasks.create_task("done", Priority::Low, "Ana").task_id;
        tasks.claim_task(claimed, "Ana");
        tasks.complete_task(done);
        CHECK_EQ(tasks.list_tasks(std::nullopt).size(), 3u);
        CHECK_EQ(tasks.list_tasks(TaskStatus::Open).size(), 1u);
        CHECK_EQ(tasks.list_tasks(TaskStatus::Claimed).size(), 1u);
        CHECK_EQ(tasks.list_tasks(TaskStatus::Done).size(), 1u);
    });

    // ------------------------------------------------------ session registry
    testing::run("U-S1", "registry: unique session ids; NAME_IN_USE logic; name freed on removal", [] {
        SessionRegistry registry;
        const std::string s1 = registry.next_session_id();
        const std::string s2 = registry.next_session_id();
        CHECK(s1 != s2);
        registry.add(s1);
        registry.add(s2);
        CHECK(registry.reserve_name(s1, "Ana"));
        CHECK(!registry.reserve_name(s2, "Ana"));      // held by an active session
        CHECK(registry.reserve_name(s2, "Ben"));
        registry.remove(s1);                            // graceful or abrupt end
        CHECK(registry.reserve_name(s2, "Ana"));
        CHECK_EQ(registry.active_count(), 1u);
    });

    // ----------------------------------------------------------- dispatcher
    testing::run("T-01u", "dispatcher: HELLO returns session_id, server_version 1, 9 commands", [] {
        Rig rig;
        const Json r = rig.request("r1", "HELLO", "{\"display_name\":\"Ana\"}");
        CHECK_EQ(Rig::status_of(r), std::string("OK"));
        CHECK_EQ(r.find("request_id")->string_value(), std::string("r1"));
        const Json* payload = r.find("payload");
        CHECK(!payload->find("session_id")->string_value().empty());
        CHECK_EQ(payload->find("server_version")->int_value(), 1);
        CHECK_EQ(payload->find("commands")->items().size(), 9u);
        CHECK(rig.session.state() == SessionState::Ready);
    });
    testing::run("U-D1", "dispatcher: commands before HELLO give INVALID_STATE; HELP and QUIT allowed", [] {
        Rig rig;
        for (const char* command : {"LIST_TASKS", "CREATE_TASK", "CLAIM_TASK", "RELEASE_TASK",
                                    "COMPLETE_TASK", "DELETE_TASK"}) {
            const Json r = rig.request("x", command, "{}");
            CHECK_EQ(error_code(r), std::string("INVALID_STATE"));
        }
        CHECK(rig.session.state() == SessionState::Connected);   // connection not closed
        CHECK_EQ(Rig::status_of(rig.request("h", "HELP", "{}")), std::string("OK"));
    });
    testing::run("U-D2", "dispatcher: second HELLO is INVALID_STATE; failed HELLO allows retry", [] {
        Rig rig;
        CHECK_EQ(error_code(rig.request("a", "HELLO", "{\"display_name\":\"bad!name\"}")),
                 std::string("INVALID_FIELD"));
        CHECK(rig.session.state() == SessionState::Connected);   // stays CONNECTED
        CHECK_EQ(error_code(rig.request("b", "HELLO", "{}")), std::string("MISSING_FIELD"));
        CHECK_EQ(error_code(rig.request("c", "HELLO", "{\"display_name\":7}")), std::string("INVALID_FIELD"));
        CHECK_EQ(Rig::status_of(rig.request("d", "HELLO", "{\"display_name\":\"Ana\"}")), std::string("OK"));
        CHECK_EQ(error_code(rig.request("e", "HELLO", "{\"display_name\":\"Ana\"}")), std::string("INVALID_STATE"));
        CHECK(rig.session.state() == SessionState::Ready);
    });
    testing::run("U-D3", "dispatcher: NAME_IN_USE when another active session holds the name", [] {
        Rig rig;
        CHECK(rig.sessions.reserve_name("sess-other", "Ana"));
        const Json r = rig.request("a", "HELLO", "{\"display_name\":\"Ana\"}");
        CHECK_EQ(error_code(r), std::string("NAME_IN_USE"));
        CHECK_EQ(r.find("message")->string_value(), std::string("The display name 'Ana' is already in use."));
        CHECK_EQ(r.find("payload")->find("field")->string_value(), std::string("display_name"));
        CHECK(rig.session.state() == SessionState::Connected);
    });
    testing::run("T-05u", "dispatcher: unknown command -> UNKNOWN_COMMAND, session still works", [] {
        Rig rig;
        rig.hello();
        const Json r = rig.request("r5", "MAKE_COFFEE", "{}");
        CHECK_EQ(error_code(r), std::string("UNKNOWN_COMMAND"));
        CHECK_EQ(r.find("request_id")->string_value(), std::string("r5"));
        CHECK_EQ(Rig::status_of(rig.request("r6", "LIST_TASKS", "{}")), std::string("OK"));
    });
    testing::run("T-06u", "dispatcher: missing/invalid fields and NOT_FOUND leave state unchanged", [] {
        Rig rig;
        rig.hello();
        CHECK_EQ(Rig::status_of(rig.request("c", "CREATE_TASK", "{\"title\":\"Keep me\"}")), std::string("OK"));

        Json r = rig.request("a", "CREATE_TASK", "{}");
        CHECK_EQ(error_code(r), std::string("MISSING_FIELD"));
        CHECK_EQ(r.find("payload")->find("field")->string_value(), std::string("title"));
        CHECK_EQ(error_code(rig.request("b", "CREATE_TASK", "{\"title\":\"x\",\"priority\":\"URGENT\"}")), std::string("INVALID_FIELD"));
        CHECK_EQ(error_code(rig.request("c2", "CREATE_TASK", "{\"title\":\"   \"}")), std::string("INVALID_FIELD"));
        CHECK_EQ(error_code(rig.request("c3", "CREATE_TASK", "{\"title\":5}")), std::string("INVALID_FIELD"));
        CHECK_EQ(error_code(rig.request("d", "CLAIM_TASK", "{\"task_id\":\"abc\"}")), std::string("INVALID_FIELD"));
        CHECK_EQ(error_code(rig.request("e", "CLAIM_TASK", "{\"task_id\":1.5}")), std::string("INVALID_FIELD"));
        CHECK_EQ(error_code(rig.request("f", "CLAIM_TASK", "{\"task_id\":0}")), std::string("INVALID_FIELD"));
        CHECK_EQ(error_code(rig.request("g", "CLAIM_TASK", "{\"task_id\":-4}")), std::string("INVALID_FIELD"));
        CHECK_EQ(error_code(rig.request("h", "CLAIM_TASK", "{}")), std::string("MISSING_FIELD"));
        CHECK_EQ(error_code(rig.request("i", "LIST_TASKS", "{\"status\":\"INVALID\"}")), std::string("INVALID_FIELD"));
        CHECK_EQ(error_code(rig.request("j", "HELP", "{\"command\":\"ARCHIVE\"}")), std::string("INVALID_FIELD"));
        CHECK_EQ(error_code(rig.request("k", "CLAIM_TASK", "{\"task_id\":9999}")), std::string("NOT_FOUND"));
        CHECK_EQ(error_code(rig.request("l", "DELETE_TASK", "{\"task_id\":9999}")), std::string("NOT_FOUND"));

        const Json list = rig.request("m", "LIST_TASKS", "{}");
        CHECK_EQ(list.find("payload")->find("count")->int_value(), 1);   // nothing changed
        const Json& only = list.find("payload")->find("tasks")->items()[0];
        CHECK_EQ(only.find("status")->string_value(), std::string("OPEN"));
        CHECK(only.find("claimed_by")->is_null());
    });
    testing::run("U-D4", "dispatcher: default priority is MEDIUM; title is trimmed; created_by is the session name", [] {
        Rig rig;
        rig.hello("Ana");
        const Json r = rig.request("a", "CREATE_TASK", "{\"title\":\"  Padded title \"}");
        const Json* task = r.find("payload")->find("task");
        CHECK_EQ(task->find("priority")->string_value(), std::string("MEDIUM"));
        CHECK_EQ(task->find("title")->string_value(), std::string("Padded title"));
        CHECK_EQ(task->find("created_by")->string_value(), std::string("Ana"));
        CHECK_EQ(task->find("status")->string_value(), std::string("OPEN"));
    });
    testing::run("U-D5", "dispatcher: CONFLICT payloads report current owner and status", [] {
        Rig rig;
        rig.hello("Ana");
        rig.request("c", "CREATE_TASK", "{\"title\":\"T\"}");
        CHECK_EQ(Rig::status_of(rig.request("a", "CLAIM_TASK", "{\"task_id\":1}")), std::string("OK"));
        const Json conflict = rig.request("b", "CLAIM_TASK", "{\"task_id\":1}");
        CHECK_EQ(error_code(conflict), std::string("CONFLICT"));
        CHECK_EQ(conflict.find("payload")->find("claimed_by")->string_value(), std::string("Ana"));
        CHECK_EQ(conflict.find("payload")->find("status")->string_value(), std::string("CLAIMED"));
        CHECK_EQ(conflict.find("message")->string_value(), std::string("Task 1 is already claimed by Ana."));
        const Json released = rig.request("r", "RELEASE_TASK", "{\"task_id\":1}");
        CHECK_EQ(released.find("payload")->find("released_by")->string_value(), std::string("Ana"));
        CHECK_EQ(error_code(rig.request("r2", "RELEASE_TASK", "{\"task_id\":1}")), std::string("CONFLICT"));
        CHECK_EQ(Rig::status_of(rig.request("d", "COMPLETE_TASK", "{\"task_id\":1}")), std::string("OK"));
        CHECK_EQ(error_code(rig.request("d2", "COMPLETE_TASK", "{\"task_id\":1}")), std::string("CONFLICT"));
        const Json del = rig.request("x", "DELETE_TASK", "{\"task_id\":1}");
        CHECK(del.find("payload")->find("deleted")->bool_value());
    });
    testing::run("U-D6", "dispatcher: blank and oversize frames give INVALID_MESSAGE / MESSAGE_TOO_LARGE", [] {
        Rig rig;
        rig.hello();
        const Json blank = rig.send_frame(FrameStatus::Blank);
        CHECK_EQ(error_code(blank), std::string("INVALID_MESSAGE"));
        CHECK(blank.find("request_id")->is_null());
        CHECK_EQ(error_code(rig.send_frame(FrameStatus::TooLarge)), std::string("MESSAGE_TOO_LARGE"));
        CHECK_EQ(error_code(rig.send("{\"version\":1,")), std::string("INVALID_MESSAGE"));
        CHECK_EQ(Rig::status_of(rig.request("ok", "LIST_TASKS", "{}")), std::string("OK"));  // still serving
    });
    testing::run("U-D7", "dispatcher: internal failure -> SERVER_ERROR with no internals leaked", [] {
        Logger logger;
        logger.set_console_echo(false);
        FailingTaskManager tasks;
        SessionRegistry sessions;
        RequestDispatcher dispatcher(tasks, sessions, logger);
        ClientSession session(sessions.next_session_id(), "127.0.0.1:1");
        sessions.add(session.id());
        auto send = [&](const std::string& line) {
            Frame frame;
            frame.line = line;
            return Rig::parse(dispatcher.handle_frame(session, frame));
        };
        send("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"h\",\"command\":\"HELLO\",\"payload\":{\"display_name\":\"Ana\"}}");
        const Json r = send("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"c\",\"command\":\"CREATE_TASK\",\"payload\":{\"title\":\"x\"}}");
        CHECK_EQ(error_code(r), std::string("SERVER_ERROR"));
        const std::string text = r.dump();
        CHECK(text.find("secret") == std::string::npos);
        CHECK(text.find(".cpp") == std::string::npos);
        // The session survives the internal error.
        const Json list = send("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"l\",\"command\":\"LIST_TASKS\",\"payload\":{}}");
        CHECK_EQ(Rig::status_of(list), std::string("OK"));
    });
    testing::run("U-D8", "dispatcher: QUIT moves the session to CLOSING; malformed QUIT does not", [] {
        Rig rig;
        rig.hello();
        CHECK_EQ(error_code(rig.request("q0", "QUIT", "{\"reason\":5}")), std::string("INVALID_FIELD"));
        CHECK(rig.session.state() == SessionState::Ready);
        const Json r = rig.request("q1", "QUIT", "{\"reason\":\"done for today\"}");
        CHECK_EQ(Rig::status_of(r), std::string("OK"));
        CHECK(rig.session.state() == SessionState::Closing);
    });
    testing::run("U-D9", "dispatcher: HELP lists all 9 commands, or one when asked", [] {
        Rig rig;
        const Json all = rig.request("a", "HELP", "{}");
        CHECK_EQ(all.find("payload")->find("commands")->items().size(), 9u);
        const Json one = rig.request("b", "HELP", "{\"command\":\"CLAIM_TASK\"}");
        CHECK_EQ(one.find("payload")->find("commands")->items().size(), 1u);
    });

    // ------------------------------------------------------- logger helper
    testing::run("U-L1", "logger sanitises client text (control characters, length)", [] {
        CHECK_EQ(Logger::sanitize("a\nb\x01" "c"), std::string("a?b?c"));
        CHECK_EQ(Logger::sanitize(std::string(100, 'x'), 10), std::string(10, 'x') + "...");
    });

    // ----------------------------------------------------- client: display
    testing::run("U-C1", "client table fits in 80 columns (long titles, long owners, big ids)", [] {
        std::vector<Task> tasks;
        tasks.push_back(make_task(1, "Short"));
        tasks.push_back(make_task(2, std::string(120, 'w')));  // one unbreakable 120-char word
        std::string long_title;
        for (int i = 0; i < 20; ++i) long_title += "wordy section ";
        tasks.push_back(make_task(1234567, long_title, "A.Very-Long_Display Name4"));
        const std::string table = format_task_table(tasks, "");
        CHECK(widest_line(table) <= 80);
        CHECK(table.find("3 tasks") != std::string::npos);
        CHECK(table.find("A.Very-Long_") != std::string::npos);
    });
    testing::run("U-C2", "client: empty list messages", [] {
        CHECK_EQ(format_task_table({}, ""), std::string("No tasks to show.\n"));
        CHECK_EQ(format_task_table({}, "OPEN"), std::string("No tasks with status OPEN.\n"));
    });
    testing::run("U-C3", "client: errors show code + sentence, fit 80 columns, no internals", [] {
        const std::string text = format_error("CONFLICT", std::string("Task 1 is already claimed by ") + std::string(24, 'N') + " and so on and on and on and on.");
        CHECK(text.find("Error [CONFLICT]") != std::string::npos);
        CHECK(widest_line(text) <= 80);
        CHECK(widest_line(format_local_help()) <= 80);
        CHECK(widest_line(format_task_detail(make_task(1, std::string(200, 'z') + " tail", "Ana"))) <= 80);
    });
    testing::run("U-C4", "client: task_from_json accepts good tasks and rejects broken ones", [] {
        Task task;
        CHECK(task_from_json(task_to_json(make_task(5, "T", "Ben")), task));
        CHECK_EQ(task.task_id, 5);
        CHECK_EQ(*task.claimed_by, std::string("Ben"));
        CHECK(!task_from_json(parse_json("{\"task_id\":1}"), task));
        CHECK(!task_from_json(parse_json("[]"), task));
    });

    // ------------------------------------------------ client: input parsing
    testing::run("U-C0", "client input: empty line is Empty; unknown verb is Invalid (local, no network)", [] {
        CHECK(parse_user_command("").kind == UserCommandKind::Empty);
        CHECK(parse_user_command("   \t ").kind == UserCommandKind::Empty);
        const UserCommand unknown = parse_user_command("make_coffee");
        CHECK(unknown.kind == UserCommandKind::Invalid);
        CHECK(unknown.error.find("help") != std::string::npos);
    });
    testing::run("U-C5", "client input: task ids are validated locally", [] {
        CHECK(parse_user_command("claim 7").task_id == 7);
        CHECK(parse_user_command("CLAIM 7").kind == UserCommandKind::Claim);
        CHECK(parse_user_command("claim").kind == UserCommandKind::Claim);          // will prompt
        CHECK(!parse_user_command("claim").task_id.has_value());
        CHECK(parse_user_command("claim abc").kind == UserCommandKind::Invalid);
        CHECK(parse_user_command("claim 0").kind == UserCommandKind::Invalid);
        CHECK(parse_user_command("claim -2").kind == UserCommandKind::Invalid);
        CHECK(parse_user_command("claim 1 2").kind == UserCommandKind::Invalid);
        CHECK(parse_user_command("release 3").kind == UserCommandKind::Release);
        CHECK(parse_user_command("done 3").kind == UserCommandKind::Done);
        CHECK(parse_user_command("delete 3").kind == UserCommandKind::Delete);
    });
    testing::run("U-C6", "client input: list filters, add shorthand, help forms, quit", [] {
        CHECK(parse_user_command("list").kind == UserCommandKind::List);
        CHECK(*parse_user_command("list open").status == "OPEN");
        CHECK(parse_user_command("list bogus").kind == UserCommandKind::Invalid);
        const UserCommand add = parse_user_command("add  Write the intro ");
        CHECK(add.kind == UserCommandKind::Add);
        CHECK_EQ(add.title, std::string("Write the intro"));
        CHECK(parse_user_command("help").kind == UserCommandKind::Help);
        CHECK(parse_user_command("help server").kind == UserCommandKind::HelpServer);
        CHECK(*parse_user_command("help server claim_task").help_topic == "CLAIM_TASK");
        CHECK(parse_user_command("help server nope").kind == UserCommandKind::Invalid);
        CHECK(parse_user_command("quit").kind == UserCommandKind::Quit);
        const UserCommand raw = parse_user_command("raw CREATE_TASK {\"priority\":\"HIGH\"}");
        CHECK(raw.kind == UserCommandKind::Raw);
        CHECK_EQ(raw.raw_command, std::string("CREATE_TASK"));
        CHECK_EQ(raw.raw_payload_text, std::string("{\"priority\":\"HIGH\"}"));
    });

    return testing::summarize("test_units");
}
