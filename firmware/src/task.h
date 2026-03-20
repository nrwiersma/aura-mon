//
// Created by Nicholas Wiersma on 2025/10/20.
//

#pragma once

#include <cstdint>
#include <functional>
#include <queue>

using TaskFn = std::function<uint32_t(void *param)>;

struct Task {
    uint32_t nextRun;
    uint8_t  priority;
    TaskFn   func;
    void *   param;

    bool operator<(const Task &o) const {
        if (nextRun > o.nextRun) {
            return true;
        }
        if (nextRun == o.nextRun) {
            return priority < o.priority;
        }
        return false;
    }
};

class TaskQueue {
public:
    void add(TaskFn func, uint8_t priority, void *param = nullptr);
    bool runNextTask();

private:
    std::priority_queue<Task> _tasks;
};
