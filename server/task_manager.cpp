// task_manager.cpp -- see task_manager.hpp.
#include "server/task_manager.hpp"

#include "shared/timeutil.hpp"

namespace stcs {

TaskManager::TaskManager(Clock clock) : clock_(std::move(clock)) {
    if (!clock_) {
        clock_ = [] { return utc_timestamp_now(); };
    }
}

Task TaskManager::create_task(const std::string& title, Priority priority,
                              const std::string& created_by) {
    // PHASE 3: acquire task_lock_ here.
    Task task;
    task.task_id = next_task_id_++;
    task.title = title;
    task.priority = priority;
    task.status = TaskStatus::Open;
    task.claimed_by = std::nullopt;
    task.created_by = created_by;
    task.created_at = clock_();
    tasks_[task.task_id] = task;
    return task;
}

std::vector<Task> TaskManager::list_tasks(
    const std::optional<TaskStatus>& status_filter) const {
    // PHASE 3: acquire task_lock_ here (reads take the lock too).
    std::vector<Task> result;
    for (const auto& entry : tasks_) {
        if (!status_filter || entry.second.status == *status_filter) {
            result.push_back(entry.second);
        }
    }
    return result;
}

TaskResult TaskManager::claim_task(long long task_id, const std::string& claimer) {
    // PHASE 3: acquire task_lock_ here; the check and the write below must
    // stay inside the same critical section.
    TaskResult result;
    auto found = tasks_.find(task_id);
    if (found == tasks_.end()) {
        result.outcome = TaskOutcome::NotFound;
        return result;
    }
    Task& task = found->second;
    if (task.status != TaskStatus::Open) {
        result.outcome = TaskOutcome::Conflict;  // task left unchanged
        result.task = task;
        return result;
    }
    task.status = TaskStatus::Claimed;
    task.claimed_by = claimer;
    result.outcome = TaskOutcome::Ok;
    result.task = task;
    return result;
}

TaskResult TaskManager::release_task(long long task_id) {
    // PHASE 3: acquire task_lock_ here.
    TaskResult result;
    auto found = tasks_.find(task_id);
    if (found == tasks_.end()) {
        result.outcome = TaskOutcome::NotFound;
        return result;
    }
    Task& task = found->second;
    if (task.status != TaskStatus::Claimed) {
        result.outcome = TaskOutcome::Conflict;
        result.task = task;
        return result;
    }
    task.status = TaskStatus::Open;
    task.claimed_by = std::nullopt;
    result.outcome = TaskOutcome::Ok;
    result.task = task;
    return result;
}

TaskResult TaskManager::complete_task(long long task_id) {
    // PHASE 3: acquire task_lock_ here.
    TaskResult result;
    auto found = tasks_.find(task_id);
    if (found == tasks_.end()) {
        result.outcome = TaskOutcome::NotFound;
        return result;
    }
    Task& task = found->second;
    if (task.status == TaskStatus::Done) {
        result.outcome = TaskOutcome::Conflict;
        result.task = task;
        return result;
    }
    task.status = TaskStatus::Done;  // claimed_by is deliberately kept
    result.outcome = TaskOutcome::Ok;
    result.task = task;
    return result;
}

TaskResult TaskManager::delete_task(long long task_id) {
    // PHASE 3: acquire task_lock_ here.
    TaskResult result;
    auto found = tasks_.find(task_id);
    if (found == tasks_.end()) {
        result.outcome = TaskOutcome::NotFound;
        return result;
    }
    result.outcome = TaskOutcome::Ok;
    result.task = found->second;
    tasks_.erase(found);
    return result;
}

}  // namespace stcs
