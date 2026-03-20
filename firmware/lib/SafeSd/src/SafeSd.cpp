//
// SafeSd.cpp — SafeSdFile method definitions.
// Template methods (SafeSdFs::with, SafeSdFs::tryWith) remain in SafeSd.h.
//

#include "SafeSd.h"

SafeSdFile::SafeSdFile(FsFile file, SafeSdFs *owner)
    : _file(std::move(file)), _owner(owner) {}

SafeSdFile::SafeSdFile(SafeSdFile &&other) noexcept
    : _file(std::move(other._file)), _owner(other._owner) {
    other._owner = nullptr;
}

SafeSdFile &SafeSdFile::operator=(SafeSdFile &&other) noexcept {
    if (this != &other) {
        _file        = std::move(other._file);
        _owner       = other._owner;
        other._owner = nullptr;
    }
    return *this;
}

bool SafeSdFs::exists(const char *path) {
    recursive_mutex_enter_blocking(&_mu);
    auto r = _sd.exists(path);
    recursive_mutex_exit(&_mu);
    return r;
}

bool SafeSdFs::mkdir(const char *path) {
    recursive_mutex_enter_blocking(&_mu);
    auto r = _sd.mkdir(path);
    recursive_mutex_exit(&_mu);
    return r;
}

bool SafeSdFs::remove(const char *path) {
    recursive_mutex_enter_blocking(&_mu);
    auto r = _sd.remove(path);
    recursive_mutex_exit(&_mu);
    return r;
}

bool SafeSdFs::rename(const char *from, const char *to) {
    recursive_mutex_enter_blocking(&_mu);
    auto r = _sd.rename(from, to);
    recursive_mutex_exit(&_mu);
    return r;
}

SafeSdFile SafeSdFs::open(const char *path, oflag_t mode) {
    recursive_mutex_enter_blocking(&_mu);
    auto f = _sd.open(path, mode);
    recursive_mutex_exit(&_mu);
    return SafeSdFile(std::move(f), this);
}

SdCard *SafeSdFs::card() {
    recursive_mutex_enter_blocking(&_mu);
    auto r = _sd.card();
    recursive_mutex_exit(&_mu);
    return r;
}

void SafeSdFs::initErrorPrint(print_t *pr) {
    recursive_mutex_enter_blocking(&_mu);
    _sd.initErrorPrint(pr);
    recursive_mutex_exit(&_mu);
}


uint32_t SafeSdFile::size() {
    if (!_owner) return 0;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return 0;
    auto r = _file.size();
    recursive_mutex_exit(&_owner->_mu);
    return r;
}

bool SafeSdFile::seek(uint32_t pos) {
    if (!_owner) return false;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return false;
    auto r = _file.seek(pos);
    recursive_mutex_exit(&_owner->_mu);
    return r;
}

int SafeSdFile::read() {
    if (!_owner) return -1;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return -1;
    auto r = _file.read();
    recursive_mutex_exit(&_owner->_mu);
    return r;
}

size_t SafeSdFile::read(void *buf, size_t sz) {
    if (!_owner) return 0;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return 0;
    auto r = _file.read(buf, sz);
    recursive_mutex_exit(&_owner->_mu);
    return r;
}

size_t SafeSdFile::write(const void *buf, size_t sz) {
    if (!_owner) return 0;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return 0;
    auto r = _file.write(buf, sz);
    recursive_mutex_exit(&_owner->_mu);
    return r;
}

size_t SafeSdFile::write(const char *str) {
    if (!_owner) return 0;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return 0;
    auto r = _file.write(str);
    recursive_mutex_exit(&_owner->_mu);
    return r;
}

bool SafeSdFile::flush() {
    if (!_owner) return false;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return false;
    _file.flush();
    recursive_mutex_exit(&_owner->_mu);
    return true;
}

void SafeSdFile::close() {
    if (!_owner) return;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return;
    _file.close();
    recursive_mutex_exit(&_owner->_mu);
}

bool SafeSdFile::isDirectory() {
    if (!_owner) return false;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return false;
    auto r = _file.isDirectory();
    recursive_mutex_exit(&_owner->_mu);
    return r;
}

void SafeSdFile::truncate() {
    if (!_owner) return;
    if (!recursive_mutex_enter_timeout_ms(&_owner->_mu, kLockTimeoutMs)) return;
    _file.truncate();
    recursive_mutex_exit(&_owner->_mu);
}
