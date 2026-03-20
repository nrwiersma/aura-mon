//
// Created by Nicholas Wiersma on 2026/03/19.
//

#pragma once

#include <Arduino.h>
#include <SdFat.h>
#include <pico/mutex.h>
#include <expected>
#include <type_traits>

class SafeSdFs;

// Wraps FsFile with per-call recursive mutex locking. Safe to call from
// either core. Returns safe defaults (0, false, -1) if the lock is not
// acquired within kLockTimeoutMs. When called inside a SafeSdFs::with()
// callback, methods re-enter the already-held mutex for free.
class SafeSdFile {
public:
    static constexpr uint32_t kLockTimeoutMs = 100;

    SafeSdFile() = default;

    // Move-only — two instances sharing an FsFile would race on the position cursor.
    SafeSdFile(SafeSdFile &&other) noexcept;
    SafeSdFile &operator=(SafeSdFile &&other) noexcept;
    SafeSdFile(const SafeSdFile &)            = delete;
    SafeSdFile &operator=(const SafeSdFile &) = delete;

    // Non-locking — reads in-RAM state, no SD access.
    bool     isOpen() const { return _file.isOpen(); }
    explicit operator bool() const { return isOpen(); }

    uint32_t size();
    bool     seek(uint32_t pos);
    int      read();
    size_t   read(void *buf, size_t sz);
    size_t   write(const void *buf, size_t sz);
    size_t   write(const char *str);
    bool     flush();
    void     close();
    bool     isDirectory();
    void     truncate();

private:
    friend class SafeSdFs;
    SafeSdFile(FsFile file, SafeSdFs *owner);

    FsFile    _file;
    SafeSdFs *_owner = nullptr;
};

// Owns the SdFs instance and the recursive mutex. All operations acquire
// the mutex for their duration. Calling methods inside a with() callback
// re-enters the already-held mutex for free, making the callback atomic.
class SafeSdFs {
public:
    SafeSdFs() { recursive_mutex_init(&_mu); }

    // Call once from setup() before tasks start.
    bool begin(SdioConfig cfg) { return _sd.begin(cfg); }

    // For pre-reboot use only — ensures no SD write is in flight.
    void lockForever() { recursive_mutex_enter_blocking(&_mu); }

    bool       exists(const char *path);
    bool       mkdir(const char *path);
    bool       remove(const char *path);
    bool       rename(const char *from, const char *to);
    SafeSdFile open(const char *path, oflag_t mode);
    SdCard    *card();
    void       initErrorPrint(print_t *pr);

    // Acquire the mutex, invoke func(*this), release. Returns whatever func returns.
    template<typename F>
    auto with(F &&func) -> std::invoke_result_t<F, SafeSdFs &> {
        recursive_mutex_enter_blocking(&_mu);
        if constexpr (std::is_void_v<std::invoke_result_t<F, SafeSdFs &>>) {
            func(*this);
            recursive_mutex_exit(&_mu);
        } else {
            auto r = func(*this);
            recursive_mutex_exit(&_mu);
            return r;
        }
    }

    // Timed variant of with(). Returns std::unexpected("sd: lock timeout")
    // if the mutex is not acquired within ms milliseconds.
    template<typename F>
    auto tryWith(uint32_t ms, F &&func)
        -> std::expected<std::invoke_result_t<F, SafeSdFs &>, String> {
        using Ret = std::invoke_result_t<F, SafeSdFs &>;
        if (!recursive_mutex_enter_timeout_ms(&_mu, ms)) {
            return std::unexpected<String>("sd: lock timeout");
        }
        if constexpr (std::is_void_v<Ret>) {
            func(*this);
            recursive_mutex_exit(&_mu);
            return {};
        } else {
            auto r = func(*this);
            recursive_mutex_exit(&_mu);
            return r;
        }
    }

private:
    friend class SafeSdFile;

    SdFs              _sd;
    recursive_mutex_t _mu;
};

