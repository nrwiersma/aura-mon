//
// Created by Nicholas Wiersma on 2025/10/20.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#endif
#include "task.h"

void TaskQueue::add(TaskFn func, uint8_t priority, void *param) {
    auto t = Task{0, priority, func, param};
    _tasks.push(t);
}

bool TaskQueue::runNextTask() {
    if (_tasks.empty()) {
        return false;
    }

    auto t = _tasks.top();
    if (millis() < _tasks.top().nextRun) {
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
