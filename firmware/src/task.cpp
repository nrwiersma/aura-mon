//
// Created by Nicholas Wiersma on 2025/10/20.
//

#ifndef UNIT_TEST
#include "auramon.h"
#else
#include "../test/stubs/TestCore.h"
#endif
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

    // Record which task is about to run so another core can report it as a
    // diagnostic breadcrumb if this core stops responding.
    uintptr_t addr = UINTPTR_MAX;
    if (auto fn = t.func.target<uint32_t (*)(void *)>()) {
        addr = reinterpret_cast<uintptr_t>(*fn);
    }
    _currentTaskStartMs.store(millis(), std::memory_order_relaxed);
    _currentTaskAddr.store(addr, std::memory_order_release);

    auto nextRun = t.func(t.param);

    _currentTaskAddr.store(0, std::memory_order_release);

    if (nextRun > 0) {
        t.nextRun = nextRun + millis();
        _tasks.push(t);
    }
    return true;
}

bool taskQueue::currentTask(uint32_t *addr, uint32_t *startMs) const {
    // The address is read both before and after the start time; only if both
    // reads agree is the pair consistent (a task may finish and another start
    // between the two loads). Tasks run for milliseconds at a time, so a
    // couple of attempts is plenty.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto a = _currentTaskAddr.load(std::memory_order_acquire);
        if (a == 0) {
            return false;
        }
        const auto start = _currentTaskStartMs.load(std::memory_order_relaxed);
        if (_currentTaskAddr.load(std::memory_order_acquire) != a) {
            continue;
        }
        *addr = a == UINTPTR_MAX ? 0 : static_cast<uint32_t>(a);
        *startMs = start;
        return true;
    }
    return false;
}
