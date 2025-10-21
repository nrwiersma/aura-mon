//
// Created by Nicholas Wiersma on 2025/10/20.
//

#ifndef FIRMWARE_TASK_H
#define FIRMWARE_TASK_H

#include <cstdint>
#include <functional>
#include <queue>

typedef std::function<uint32_t(void *param)> taskFunction;

struct task {
    uint32_t nextRun;
    uint8_t priority;
    taskFunction func;
    void *param;

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
    void add(taskFunction func, uint8_t priority, void * param = nullptr);
    bool runNextTask();

private:
    std::priority_queue<task> _tasks;
};


#endif //FIRMWARE_TASK_H
