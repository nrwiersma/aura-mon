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

    bool rename(const char* oldpath, const char* newpath) {
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


// ============================================================
// SafeSd test stubs
//
// These provide SafeSdFile and SafeSdFs backed by MockSD for the
// native test environment. No real locking is performed — tests are
// single-threaded, and recursive_mutex_t is already a no-op stub.
//
// SafeSdFs exposes reference members (file, fileExists, directories)
// that alias the internal MockSD, so existing test setUp()/tearDown()
// code that accesses sd.file, sd.fileExists, sd.directories compiles
// unchanged once sd is declared as SafeSdFs.
// ============================================================

#include <expected>
#include <type_traits>

// Types used by the production SafeSd.h that are provided by SdFat on
// hardware but must be stubbed for native builds.
using oflag_t = int;
struct SdioConfig {};
struct SdCard {
    uint8_t status()    { return 1; }
    uint8_t errorCode() { return 0; }
};

// ---- Forward declaration ---------------------------------------------------
class SafeSdFs;

// ---- SafeSdFile ------------------------------------------------------------

class SafeSdFile {
public:
    SafeSdFile() = default;

    SafeSdFile(SafeSdFile &&other) noexcept
        : _file(std::move(other._file)) {}

    SafeSdFile &operator=(SafeSdFile &&other) noexcept {
        _file = std::move(other._file);
        return *this;
    }

    SafeSdFile(const SafeSdFile &)            = delete;
    SafeSdFile &operator=(const SafeSdFile &) = delete;

    bool     isOpen()   const { return _file.isOpen(); }
    explicit operator bool() const { return isOpen(); }

    uint32_t size()                       { return _file.size(); }
    bool     seek(uint32_t pos)           { return _file.seek(pos); }
    int      read()                       { return _file.read(); }
    size_t   read(void *buf, size_t sz)   { return _file.read(buf, sz); }
    size_t   write(const void *buf, size_t sz) { return _file.write(buf, sz); }
    size_t   write(const char *str)       { return _file.write(str); }
    bool     flush()                      { return _file.flush(); }
    void     close()                      { _file.close(); }
    bool     isDirectory()                { return false; }
    void     truncate()                   { _file.truncate(); }

private:
    friend class SafeSdFs;
    explicit SafeSdFile(FsFile f) : _file(std::move(f)) {}

    FsFile _file;
};

// ---- SafeSdFs --------------------------------------------------------------

class SafeSdFs {
public:
    MockSD mock;

    // Reference members — let existing test code use sd.file,
    // sd.fileExists, sd.directories without modification.
    FsFile                  *&file;
    bool                     &fileExists;
    std::vector<std::string> &directories;

    SafeSdFs()
        : mock(),
          file(mock.file),
          fileExists(mock.fileExists),
          directories(mock.directories) {}

    // SafeSdFs is not copyable (has reference members).
    SafeSdFs(const SafeSdFs &)            = delete;
    SafeSdFs &operator=(const SafeSdFs &) = delete;

    bool begin(SdioConfig) { return true; }
    void lockForever() {}

    bool       exists(const char *p)                { return mock.exists(p); }
    bool       mkdir(const char *p)                 { return mock.mkdir(p); }
    bool       remove(const char *p)                { return mock.remove(p); }
    bool       rename(const char *a, const char *b) { return mock.rename(a, b); }
    SafeSdFile open(const char *p, oflag_t m)       { return SafeSdFile(mock.open(p, static_cast<int>(m))); }
    SdCard    *card()                               { return &_card; }

    template<typename F>
    auto with(F &&func) -> std::invoke_result_t<F, SafeSdFs &> {
        if constexpr (std::is_void_v<std::invoke_result_t<F, SafeSdFs &>>) {
            func(*this);
        } else {
            return func(*this);
        }
    }

    template<typename F>
    auto tryWith(uint32_t, F &&func)
        -> std::expected<std::invoke_result_t<F, SafeSdFs &>, String> {
        using Ret = std::invoke_result_t<F, SafeSdFs &>;
        if constexpr (std::is_void_v<Ret>) {
            func(*this);
            return {};
        } else {
            return func(*this);
        }
    }

private:
    SdCard _card;
};

inline SafeSdFs sd;

