//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <map>

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
        // Matches real filesystem semantics: return whatever is left (down
        // to zero at EOF), not just an exact-fit chunk.
        if (!open || position >= data->size()) {
            return 0;
        }
        size_t available = data->size() - position;
        size_t n = sz < available ? sz : available;
        std::memcpy(buf, &(*data)[position], n);
        position += n;
        return n;
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

    // Opt-in multi-path mode: existing single-canonical-file tests
    // (test_config/test_datalog/test_logger) rely on `file`/`fileExists`
    // being agnostic of the path argument, so that behaviour is preserved
    // by default. Tests that need two independent files open at once
    // (e.g. a source file plus a temp file during a rename dance) set
    // `multiFileMode = true` and get real per-path storage instead.
    bool multiFileMode = false;
    std::map<std::string, FsFile*> namedFiles;
    std::map<std::string, bool> namedFileExists;

    MockSD() : file(nullptr), fileExists(false) {}

    ~MockSD() {
        if (file) delete file;
        for (auto &kv : namedFiles) {
            delete kv.second;
        }
    }

    bool exists(const char* path) {
        if (multiFileMode) {
            auto it = namedFileExists.find(path);
            return it != namedFileExists.end() && it->second;
        }
        return fileExists;
    }

    bool mkdir(const char* path) {
        directories.push_back(path);
        return true;
    }

    bool remove(const char* path) {
        if (multiFileMode) {
            auto it = namedFiles.find(path);
            if (it != namedFiles.end()) {
                it->second->data->clear();
                it->second->open = false;
            }
            namedFileExists[path] = false;
            return true;
        }
        if (file) {
            file->data->clear();
            file->open = false;
        }
        fileExists = false;
        return true;
    }

    bool rename(const char* oldpath, const char* newpath) {
        if (multiFileMode) {
            auto it = namedFiles.find(oldpath);
            if (it == namedFiles.end()) {
                return false;
            }
            if (namedFiles.count(newpath)) {
                delete namedFiles[newpath];
            }
            namedFiles[newpath] = it->second;
            namedFileExists[newpath] = true;
            namedFiles.erase(it);
            namedFileExists[oldpath] = false;
            return true;
        }
        return true;
    }

    FsFile open(const char* path, int mode) {
        // Real SdFat only seeks to end for append-style opens (O_RDWR, as
        // used by FILE_WRITE); a pure O_READ open starts at position 0.
        const bool appendPosition = (mode & O_RDWR) != 0;

        if (multiFileMode) {
            FsFile* target;
            auto it = namedFiles.find(path);
            if (it == namedFiles.end()) {
                target = new FsFile();
                namedFiles[path] = target;
            } else {
                target = it->second;
            }
            FsFile handle;
            handle.data = target->data;
            handle.open = true;
            handle.position = appendPosition ? static_cast<uint32_t>(target->data->size()) : 0;
            namedFileExists[path] = true;
            return handle;
        }

        if (!file) {
            file = new FsFile();
        }
        // Return a copy that shares the same backing data.
        FsFile handle;
        handle.data = file->data;
        handle.open = true;
        handle.position = appendPosition ? static_cast<uint32_t>(file->data->size()) : 0;
        fileExists = true;
        return handle;
    }
};

inline mutex_t sdMu;
inline MockSD sd;
