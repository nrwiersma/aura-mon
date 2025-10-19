//
// Created by Nicholas Wiersma on 2025/10/15.
//

#include <FreeRTOS.h>
#include <queue.h>
#include <time.h>
#include "SyncFS.h"

struct fsOpParams {
    const char *const path;
    void *const data;
    size_t size = 0;
    uint8_t offset = 0;
    bool cleanup = false;
};

struct fsTaskMessage {
    enum Op: uint8_t {
        MKDIR, EXISTS, REMOVE, RENAME, STAT,READ_FILE, WRITE_FILE, APPEND_FILE
    } op;

    TaskHandle_t task;
    fsOpParams params;
};

static TaskHandle_t __fsTask;

void fsTask(void *param) {
    const SyncFS *syncFS = static_cast<SyncFS *>(param);
    fsTaskMessage *msg = nullptr;
    size_t size;
    bool ok;
    char *str;
    uint8_t *buf;

    while (true) {
        if (pdFALSE == xQueueReceive(syncFS->_queue, &msg, portMAX_DELAY)) {
            continue;
        }

        switch (msg->op) {
            case fsTaskMessage::MKDIR:
                ok = syncFS->_mkdir(msg->params.path);
                xTaskNotify(msg->task, ok, eSetValueWithOverwrite);
                break;
            case fsTaskMessage::EXISTS:
                ok = syncFS->_exists(msg->params.path);
                xTaskNotify(msg->task, ok, eSetValueWithOverwrite);
                break;
            case fsTaskMessage::REMOVE:
                ok = syncFS->_remove(msg->params.path);
                xTaskNotify(msg->task, ok, eSetValueWithOverwrite);
                break;
            case fsTaskMessage::RENAME:
                ok = syncFS->_rename(msg->params.path, static_cast<char *>(msg->params.data));
                xTaskNotify(msg->task, ok, eSetValueWithOverwrite);
                break;
            case fsTaskMessage::STAT:
                ok = syncFS->_stat(msg->params.path, static_cast<FileInfo *>(msg->params.data));
                xTaskNotify(msg->task, ok, eSetValueWithOverwrite);
                break;
            case fsTaskMessage::READ_FILE:
                size = syncFS->_readFile(msg->params.path, msg->params.data, msg->params.size,
                                         msg->params.offset);
                xTaskNotify(msg->task, size, eSetValueWithOverwrite);
                break;
            case fsTaskMessage::WRITE_FILE:
                size = syncFS->_writeFile(msg->params.path, msg->params.data, msg->params.size,
                                          msg->params.offset);
                xTaskNotify(msg->task, size, eSetValueWithOverwrite);
                break;
            case fsTaskMessage::APPEND_FILE:
                str = static_cast<char *>(msg->params.data);
                size = syncFS->_appendFile(msg->params.path, str, msg->params.size);
                if (msg->task) {
                    xTaskNotify(msg->task, size, eSetValueWithOverwrite);
                }
                if (msg->params.cleanup) {
                    delete[] str;
                }
                break;
        }
        delete msg;
    }
}

static time_t fatToTimeT(uint16_t d, uint16_t t);

bool SyncFS::begin(SdFs &fs) {
    _fs = &fs;
    _queue = xQueueCreate(10, sizeof(fsTaskMessage*));
    if (!_queue) {
        return false;
    }
    if (pdPASS != xTaskCreate(fsTask, "SyncFS", 1024, this, configMAX_PRIORITIES - 2, &__fsTask)) {
        return false;
    }
    return true;
}

void SyncFS::end() const {
    vTaskDelete(__fsTask);
    if (_fs) {
        _fs->end();
    }
}

bool SyncFS::mkdir(const char *path) const {
    const auto args = fsOpParams{path};
    auto *msg = new fsTaskMessage{fsTaskMessage::MKDIR, xTaskGetCurrentTaskHandle(), args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
    return ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

bool SyncFS::exists(const char *path) const {
    const auto args = fsOpParams{path};
    auto *msg = new fsTaskMessage{fsTaskMessage::EXISTS, xTaskGetCurrentTaskHandle(), args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
    return ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

bool SyncFS::remove(const char *path) const {
    const auto args = fsOpParams{path};
    auto *msg = new fsTaskMessage{fsTaskMessage::REMOVE, xTaskGetCurrentTaskHandle(), args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
    return ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

bool SyncFS::rename(const char *pathFrom, const char *pathTo) const {
    const auto args = fsOpParams{pathFrom, &pathTo};
    auto *msg = new fsTaskMessage{fsTaskMessage::RENAME, xTaskGetCurrentTaskHandle(), args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
    return ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

bool SyncFS::stat(const char *path, FileInfo *info) const {
    const auto args = fsOpParams{path, info};
    auto *msg = new fsTaskMessage{fsTaskMessage::STAT, xTaskGetCurrentTaskHandle(), args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
    return ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

size_t SyncFS::readFile(const char *path, void *out, const size_t size, uint8_t offset) const {
    const auto args = fsOpParams{path, out, size, offset};
    auto *msg = new fsTaskMessage{fsTaskMessage::READ_FILE, xTaskGetCurrentTaskHandle(), args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
    return ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

size_t SyncFS::writeFile(const char *path, void *data, size_t size, uint8_t offset) const {
    const auto args = fsOpParams{path, data, size, offset};
    auto *msg = new fsTaskMessage{fsTaskMessage::WRITE_FILE, xTaskGetCurrentTaskHandle(), args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
    return ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

size_t SyncFS::appendFile(const char *path, void *data, size_t size) const {
    const auto args = fsOpParams{path, data, size};
    auto *msg = new fsTaskMessage{fsTaskMessage::APPEND_FILE, xTaskGetCurrentTaskHandle(), args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
    return ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

void SyncFS::appendFileAsync(const char *path, char *data, size_t size) const {
    const auto args = fsOpParams{path, data, size, 0, true};
    auto *msg = new fsTaskMessage{fsTaskMessage::APPEND_FILE, nullptr, args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
}

void SyncFS::appendFileAsync(const char *path, const char *str) const {
    const auto args = fsOpParams{path, (void *) str, strlen(str)};
    auto *msg = new fsTaskMessage{fsTaskMessage::APPEND_FILE, nullptr, args};
    if (!xQueueSend(_queue, &msg, portMAX_DELAY)) {
        panic("SyncFS task send failed");
    }
}

bool SyncFS::_mkdir(const char *path) const {
    return _fs->mkdir(path);
}

bool SyncFS::_exists(const char *path) const {
    return _fs->exists(path);
}

bool SyncFS::_rename(const char *pathFrom, const char *pathTo) const {
    return _fs->rename(pathFrom, pathTo);
}

bool SyncFS::_remove(const char *path) const {
    if (!_exists(path)) return false;
    return _fs->remove(path);
}

bool SyncFS::_stat(const char *path, FileInfo *info) const {
    if (!_exists(path)) return false;

    FsFile f = _fs->open(path, O_RDONLY);
    if (!f) return false;
    info->path = path;
    info->size = f.size();
    info->isDir = f.isDir();

    uint16_t date, time;
    f.getModifyDateTime(&date, &time);
    info->modTime = fatToTimeT(date, time);

    return true;
}

size_t SyncFS::_readFile(const char *path, void *out, size_t size, uint8_t offset) const {
    if (!_exists(path)) return 0;

    FsFile f = _fs->open(path, O_RDONLY);
    if (offset > 0) {
        f.seek(offset);
    }
    const size_t n = f.read(out, size);
    f.close();
    return n;
}

size_t SyncFS::_writeFile(const char *path, const void *data, size_t size, uint8_t offset) const {
    bool trunc = offset == 0;

    oflag_t flags = O_WRITE | O_CREAT | O_TRUNC;
    if (!trunc) {
        if (!_exists(path)) return 0;

        flags = O_WRITE | O_CREAT | O_APPEND;
    }

    FsFile f = _fs->open(path, flags);
    if (!trunc) {
        f.seek(offset);
    }
    const size_t n = f.write(data, size);
    f.close();
    return n;
}

size_t SyncFS::_appendFile(const char *path, const void *data, size_t size) const {
    FsFile f = _fs->open(path, O_WRITE | O_CREAT | O_APPEND);
    if (!f) return 0;
    const size_t n = f.write(data, size);
    f.close();
    return n;
}

static time_t fatToTimeT(uint16_t d, uint16_t t) {
    struct tm tiempo;
    memset(&tiempo, 0, sizeof(tiempo));
    tiempo.tm_sec = (((int) t) << 1) & 0x3e;
    tiempo.tm_min = (((int) t) >> 5) & 0x3f;
    tiempo.tm_hour = (((int) t) >> 11) & 0x1f;
    tiempo.tm_mday = (int) (d & 0x1f);
    tiempo.tm_mon = ((int) (d >> 5) & 0x0f) - 1;
    tiempo.tm_year = ((int) (d >> 9) & 0x7f) + 80;
    tiempo.tm_isdst = -1;
    return mktime(&tiempo);
}
