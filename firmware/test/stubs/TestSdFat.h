//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include "TestPlatform.h"
#include <vector>

// Simple in-memory file stub
class FsFile {
public:
    std::vector<uint8_t> data;
    uint32_t position;
    bool open;

    FsFile() : position(0), open(false) {}

    bool isOpen() const { return open; }

    operator bool() const { return isOpen(); }

    uint32_t size() { return data.size(); }

    bool seek(uint32_t pos) {
        if (pos > data.size()) {
            return false;
        }
        position = pos;
        return true;
    }

    size_t read(void* buf, size_t size) {
        if (!open || position + size > data.size()) {
            return 0;
        }
        std::memcpy(buf, &data[position], size);
        position += size;
        return size;
    }

    size_t write(const void* buf, size_t size) {
        if (!open) return 0;

        // Resize if needed
        if (position + size > data.size()) {
            data.resize(position + size);
        }

        std::memcpy(&data[position], buf, size);
        position += size;
        return size;
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
            file->data.clear();
            file->open = false;
        }
        fileExists = false;
        return true;
    }

    FsFile open(const char* path, int mode) {
        if (!file) {
            file = new FsFile();
        }
        file->open = true;
        file->position = 0;
        fileExists = true;
        return *file;
    }
};

inline mutex_t sdMu;
inline MockSD sd;
