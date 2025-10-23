//
// Created by Nicholas Wiersma on 2025/10/20.
//

#include "auramon.h"
#include "task.h"

void taskQueue::add(taskFunction func, uint8_t priority, void *param) {
    auto t = task{0, priority, func, param};
    _tasks.push(t);
}

bool taskQueue::runNextTask() {
    if (_tasks.empty()) {
        return false;
    }

    // Check if the next task is ready to run.
    auto t = _tasks.top();
    if (millis() < _tasks.top().nextRun) {
        // The task is not ready to run yet.
        return false;
    }
    _tasks.pop();

    auto nextRun = t.func(t.param);
    if (nextRun > 0) {
        t.nextRun = nextRun + millis();
        _tasks.push(t);
    }
    return true;
}
