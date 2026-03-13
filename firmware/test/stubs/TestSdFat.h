//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include <cstdint>
#include <string>
#include <memory>

#include "TestPlatform.h"
#include <vector>

// Simple in-memory file stub
class FsFile {
public:
    // Shared backing buffer so copies of FsFile (returned by MockSD::open)
    // all write into the same memory.
    std::shared_ptr<std::vector<uint8_t>> data;
    uint32_t position;
    bool open;

    FsFile() : data(std::make_shared<std::vector<uint8_t>>()), position(0), open(false) {}

    bool isOpen() const { return open; }

    operator bool() const { return isOpen(); }

    uint32_t size() { return data->size(); }

    bool seek(uint32_t pos) {
        if (pos > data->size()) {
            return false;
        }
        position = pos;
        return true;
    }

    int read() {
        if (!open || position + 1 > data->size()) {
            return -1;
        }
        uint8_t b = (*data)[position];
        position += 1;
        return 1;
    }

    size_t read(void* buf, size_t sz) {
        if (!open || position + sz > data->size()) {
            return 0;
        }
        std::memcpy(buf, &(*data)[position], sz);
        position += sz;
        return sz;
    }

    void truncate() {
        data->resize(0);
        position = 0;
    }

    size_t write(const void* buf, size_t sz) {
        if (!open) return 0;
        if (position + sz > data->size()) {
            data->resize(position + sz);
        }
        std::memcpy(&(*data)[position], buf, sz);
        position += sz;
        return sz;
    }

    size_t write(const char* str) {
        if (!open || !str) return 0;
        size_t len = std::strlen(str);
        return write(static_cast<const void*>(str), len);
    }

    size_t write(uint8_t c) {
        if (!open) return 0;
        if (position + 1 > data->size()) {
            data->resize(position + 1);
        }
        (*data)[position] = c;
        position += 1;
        return 1;
    }

    bool flush() { return true; }

    void close() {
        open = false;
    }
};

// Simple file system stub
class MockSD {
public:
    FsFile* file;
    std::vector<std::string> directories;
    bool fileExists;

    MockSD() : file(nullptr), fileExists(false) {}

    ~MockSD() {
        if (file) delete file;
    }

    bool exists(const char* path) {
        return fileExists;
    }

    bool mkdir(const char* path) {
        directories.push_back(path);
        return true;
    }

    bool remove(const char* path) {
        if (file) {
            file->data->clear();
            file->open = false;
        }
        fileExists = false;
        return true;
    }

    FsFile open(const char* path, int mode) {
        if (!file) {
            file = new FsFile();
        }
        // Return a copy that shares the same backing data.
        FsFile handle;
        handle.data = file->data;
        handle.open = true;
        // Seek to end for append-style writes (FILE_WRITE behaviour).
        handle.position = static_cast<uint32_t>(file->data->size());
        fileExists = true;
        return handle;
    }
};

inline mutex_t sdMu;
inline MockSD sd;
