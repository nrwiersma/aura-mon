//
// Created by Nicholas Wiersma on 2026/09/11.
//

#pragma once

#include "auramon.h"

#include <HTTPRequest.h>

struct deviceColumn {
    uint8_t index;
    String  name;
};

class csvQuery {
public:
    // Takes ownership of req.
    csvQuery(uint32_t start, uint32_t end, uint32_t intvl, HTTPRequest *req) : _start(start),
                                                                               _end(end),
                                                                               _interval(intvl),
                                                                               _req(req),
                                                                               _headerSent(false),
                                                                               _previousRecordRead(false),
                                                                               _deviceCount(0) {
    }

    ~csvQuery() {
        delete _req;
    }

    static uint32_t stepTask(void *param) {
        auto *  qry = static_cast<csvQuery *>(param);
        uint32_t next = qry->_step();
        if (next == 0) {
            delete qry;
        }
        return next;
    };

protected:
    uint32_t _step();
    void _fetchDeviceColumns();

    uint32_t     _start, _end, _interval;
    HTTPRequest *_req;

    bool         _headerSent;
    bool         _previousRecordRead;
    deviceColumn _deviceColumns[MAX_DEVICES];
    size_t       _deviceCount;
    LogRecord    _prevRec;
};
