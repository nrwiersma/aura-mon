//
// Created by Nicholas Wiersma on 2025/10/20.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <queue>

typedef std::function<uint32_t(void *param)> taskFunction;

struct task {
    uint32_t     nextRun;
    uint8_t      priority;
    taskFunction func;
    void *       param;

    bool operator<(const task &o) const {
        if (nextRun > o.nextRun) {
            return true;
        }
        if (nextRun == o.nextRun) {
            return priority < o.priority;
        }
        return false;
    }
};

class taskQueue {
public:
    void add(taskFunction func, uint8_t priority, void *param = nullptr);
    bool runNextTask();

    // currentTask reads a consistent snapshot of the task currently executing
    // on this queue.
    bool currentTask(uint32_t *addr, uint32_t *startMs) const;

private:
    std::priority_queue<task>  _tasks;
    std::atomic<uintptr_t>     _currentTaskAddr{0};
    std::atomic<uint32_t>      _currentTaskStartMs{0};
};
