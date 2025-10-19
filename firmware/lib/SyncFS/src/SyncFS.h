//
// Created by Nicholas Wiersma on 2025/10/15.
//

#ifndef FIRMWARE_SYNC_FS_H
#define FIRMWARE_SYNC_FS_H

#include <Arduino.h>
#include <SdFat.h>

struct FileInfo {
    String path;
    size_t size = 0;
    time_t modTime = 0;
    bool isDir = false;
};

class SyncFS {
public:
    SyncFS() {};

    bool begin(SdFs &fs);
    void end() const;

    bool mkdir(const char *path) const;
    bool exists(const char* path) const;
    bool remove(const char* path) const;
    bool rename(const char* pathFrom, const char* pathTo) const;
    bool stat(const char *path, FileInfo *info) const;

    size_t readFile(const char* path, void* out, size_t size, uint8_t offset = 0) const;
    size_t writeFile(const char* path, void* data, size_t size, uint8_t offset = 0) const;
    size_t appendFile(const char* path, void* data, size_t size) const;
    void appendFileAsync(const char* path, char* data, size_t size) const;
    void appendFileAsync(const char* path, const char *str) const;

protected:
    SdFs* _fs{};
    QueueHandle_t _queue{};

    bool _mkdir(const char *path) const;
    bool _exists(const char* path) const;
    bool _rename(const char* pathFrom, const char* pathTo) const;
    bool _remove(const char* path) const;
    bool _stat(const char *path, FileInfo *info) const;

    size_t _readFile(const char* path, void* out, size_t size, uint8_t offset) const;
    size_t _writeFile(const char* path, const void* data, size_t size, uint8_t offset) const;
    size_t _appendFile(const char* path,  const void* data, size_t size) const;

    friend void fsTask(void* param);
};

#endif //FIRMWARE_SYNC_FS_H