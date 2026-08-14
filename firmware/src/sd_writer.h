//
// Created by Nicholas Wiersma on 2026/08/13.
//

#pragma once

#include <cstddef>
#include <cstdint>

struct LogRecord;

// Maximum length of a queued message log line. Longer lines are truncated when
// queued.
constexpr size_t SD_MSG_MAX = 192;

// Number of datalog records that may be waiting to be written. At the default
// 5 second interval this buffers well beyond any plausible card stall.
constexpr size_t SD_REC_QUEUE_LEN = 8;

// Number of message log lines that may be waiting to be written.
constexpr size_t SD_MSG_QUEUE_LEN = 16;

void initSDWriter();

// Queue a datalog record. Records are never dropped in favour of newer ones;
// if the queue is full this returns false and the caller must retry.
bool queueLogRecord(const LogRecord *rec);

// Queue a message log line. Returns false if the line was dropped, which
// happens when the queue is full. The oldest line is discarded to make room so
// that the most recent context always survives.
bool queueMessage(const char *buf, size_t len);

// Drains queued work. This is the only task that performs deferred SD writes,
// so it must be registered on core 0 only.
uint32_t sdWriterTask(void *param);

// Writes everything still queued, blocking until the queue is empty or no
// further progress can be made. Used on shutdown.
void drainSDWriter();
