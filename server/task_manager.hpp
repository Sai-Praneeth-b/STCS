// task_manager.hpp -- the authoritative task list (application logic + data).
//
// TaskManager owns the in-memory task store (a map from task_id to Task and
// the next_task_id counter, Phase 1 section 8 "TaskStore"). It knows nothing
// about JSON, sockets or sessions: each method takes plain values and returns
// a plain result, and the dispatcher turns that result into a protocol
// response.
//
// PHASE 3 MIGRATION POINT: every public method is one complete
// check-then-act unit (it reads the task, checks its status and writes the
// change without returning in between). In Phase 3 the only edit needed is a
// `std::lock_guard<std::mutex> guard(task_lock_);` as the first statement of
// each public method; no caller changes, and no protocol change.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "shared/protocol.hpp"

namespace stcs {

enum class TaskOutcome {
    Ok,        // the change was committed
    NotFound,  // no task with that id
    Conflict,  // task exists but its status does not allow the operation
};

// For Ok and Conflict, `task` is the record as it is after the call (so a
// Conflict result reports the current owner/status without changing it).
struct TaskResult {
    TaskOutcome outcome = TaskOutcome::NotFound;
    Task task;
};

class TaskManager {
public:
    using Clock = std::function<std::string()>;

    // `clock` supplies created_at; tests inject a fixed clock.
    explicit TaskManager(Clock clock = nullptr);
    virtual ~TaskManager() = default;

    // CREATE_TASK: new id from the counter, status OPEN, claimed_by null.
    // `title` must already be validated and trimmed.
    virtual Task create_task(const std::string& title, Priority priority,
                             const std::string& created_by);

    // LIST_TASKS: ascending task_id, optionally only tasks with `status`.
    std::vector<Task> list_tasks(const std::optional<TaskStatus>& status_filter) const;

    // CLAIM_TASK: OPEN -> CLAIMED, claimed_by = `claimer`.
    TaskResult claim_task(long long task_id, const std::string& claimer);

    // RELEASE_TASK: CLAIMED -> OPEN, claimed_by = null.
    TaskResult release_task(long long task_id);

    // COMPLETE_TASK: OPEN or CLAIMED -> DONE; claimed_by is kept.
    TaskResult complete_task(long long task_id);

    // DELETE_TASK: removes the task in any status. The id is never reused
    // because next_task_id_ only ever increases.
    TaskResult delete_task(long long task_id);

    std::size_t size() const { return tasks_.size(); }

private:
    Clock clock_;
    std::map<long long, Task> tasks_;  // ordered, so listing is by task_id
    long long next_task_id_ = 1;
    // PHASE 3: std::mutex task_lock_;  (guards tasks_ and next_task_id_)
};

}  // namespace stcs
