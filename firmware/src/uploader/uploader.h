//
// Created by Nicholas Wiersma on 2026/08/20.
//

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <asyncHTTPrequest.h>
#include <errors.h>
#include <xbuf.h>

#include "config.h"

// Uploader is the base class for Core0 tasks that push data log records to a
// remote endpoint (e.g. InfluxDB2/3, Home Assistant). It owns:
//
//  - A non-blocking state machine built on AsyncHTTP (RPAsyncTCP), so a
//    request in flight never blocks the task queue, the Modbus/SD plane, or
//    other uploaders.
//  - Durable upload progress (_lastSentTS), persisted to the SD card and
//    reloaded on boot, so a reboot never re-sends or skips records.
//  - Backing off while the network is down or a request fails, without
//    advancing progress, so the data log simply accumulates until the
//    uploader catches back up - no data is lost, it is only delayed.
//
// Concrete uploaders (one per remote service) implement applySettings() and
// buildRequest() to interpret their own settings and build their own wire
// format; everything else here is shared plumbing.
class Uploader {
public:
    Uploader(String id, String type);
    virtual ~Uploader();

    // dispatch drives the state machine one step. It is registered as a
    // Core0 task (see uploader_registry.cpp) and returns the number of
    // milliseconds before it should run again, matching taskFunction.
    uint32_t dispatch();

    // configure applies a (possibly updated) configuration. Returns false if
    // the configuration is invalid; the caller should then destroy this
    // uploader. Safe to call repeatedly; settings are only re-applied when
    // the config actually changes.
    bool configure(const UploaderConfig &cfg);

    const String &id() const { return _id; }
    const String &type() const { return _type; }

    // requestStop tells the uploader to stop. It frees itself on its next
    // dispatch() call rather than immediately, so it is safe to call even
    // while a dispatch is already scheduled to run again.
    void requestStop() { _stopRequested = true; }

protected:
    // Parses/validates settings specific to the concrete uploader type.
    // Called from configure() whenever the stored settings change.
    virtual bool applySettings(JsonObjectConst settings) = 0;

    // Builds the request body for the half-open interval (fromTS, toTS].
    // Returns false if there is nothing to send (e.g. no enabled sensors,
    // or every value is unchanged), which is not treated as an error: the
    // uploader simply advances past the interval and tries again later.
    virtual bool buildRequest(uint32_t fromTS, uint32_t toTS, xbuf &body, String &contentType) = 0;

    // The endpoint to request (combined with the uploader's own base URL as
    // needed) and the HTTP method to use. Queried immediately before send.
    virtual String      endpoint() const = 0;
    virtual const char *httpMethod() const { return "POST"; }

private:
    enum class State : uint8_t { Build, Post, Wait };

    uint32_t handleBuild();
    uint32_t handlePost();
    uint32_t handleWait();

    void loadProgress();
    void saveProgress();
    void statePath(char *buf, size_t len) const;

    String   _id;
    String   _type;
    uint32_t _interval;

    State    _state;
    uint32_t _pendingToTS;
    xbuf     _body;
    String   _contentType;

    uint32_t _lastSentTS;
    bool     _progressLoaded;
    bool     _stopRequested;

    asyncHTTPrequest *_request;
};
