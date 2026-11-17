//
// Created by Nicholas Wiersma on 2026/11/17.
//

#pragma once

#include "auramon.h"

#include <HTTPRequest.h>

struct metricsDevice {
    uint8_t index;
    uint8_t addr;
    String  name;
};

// metricsQuery writes the Prometheus metrics as a chunked response, one small
// section per task step, so a slow scraper can never block core 0.
class metricsQuery {
public:
    // Takes ownership of req.
    explicit metricsQuery(HTTPRequest *req);

    ~metricsQuery();

    // active reports whether a scrape is currently being streamed.
    static bool active() { return _active; }

    static uint32_t stepTask(void *param) {
        auto *   qry = static_cast<metricsQuery *>(param);
        uint32_t next = qry->_step();
        if (next == 0) {
            delete qry;
        }
        return next;
    }

protected:
    uint32_t _step();
    void     _snapshotDevices();
    bool     _buildNextSection();

    String _modbusSection();
    String _deviceErrorsSection() const;
    String _deviceCollectTimeSection() const;
    String _datalogSection();
    String _systemSection();
    String _uploaderCountersSection();
    String _uploaderStateSection();

    static bool _active;

    HTTPRequest *_req;
    uint32_t     _startMs;
    bool         _headerSent;
    bool         _devicesSnapshotted;
    uint8_t      _section;
    String       _pending;
    metricsDevice _devices[MAX_DEVICES];
    size_t        _deviceCount;
};
