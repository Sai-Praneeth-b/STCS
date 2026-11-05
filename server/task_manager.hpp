#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "shared/protocol.hpp"

namespace stcs {

enum class TaskOutcome {
    Ok,
    NotFound,
    Conflict,
};

struct TaskResult {
    TaskOutcome outcome = TaskOutcome::NotFound;
    Task task;
};

class TaskManager {
public:
    using Clock = std::function<std::string()>;

    explicit TaskManager(Clock clock = nullptr);
    virtual ~TaskManager() = default;

    virtual Task create_task(const std::string& title, Priority priority,
                             const std::string& created_by);

    std::vector<Task> list_tasks(const std::optional<TaskStatus>& status_filter) const;

    TaskResult claim_task(long long task_id, const std::string& claimer);

    TaskResult release_task(long long task_id);

    TaskResult complete_task(long long task_id);

    TaskResult delete_task(long long task_id);

    std::size_t size() const;

private:
    // One lock guards the whole task table. Every public operation (check state,
    // change state) runs under it, so each command is atomic: of two simultaneous
    // claims exactly one wins and the other gets CONFLICT.
    mutable std::mutex mutex_;
    Clock clock_;
    std::map<long long, Task> tasks_;
    long long next_task_id_ = 1;
};

}
