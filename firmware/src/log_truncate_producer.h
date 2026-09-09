//
// Created by Nicholas Wiersma on 2026/01/23.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <HttpResponseProducer.h>

// Truncates the message log up to (and including) the last restart marker,
// the same behaviour as the previous blocking implementation, but broken
// into small bounded steps so each produce() call only touches the SD card
// for the duration of a single chunk instead of the whole operation.
//
// Every step reports Pending because the response body is empty on
// success (204 No Content) - there is nothing to stream, only a final
// Done/Error once the whole rewrite has completed.
class LogTruncateProducer : public HttpResponseProducer {
public:
    Status produce(uint8_t *buf, size_t cap, size_t &written) override;

    int statusCode() const override { return _statusCode; }
    const char *contentType() const override { return "text/plain"; }
    const char *errorReason() const override { return _errorReason.empty() ? nullptr : _errorReason.c_str(); }

private:
    static constexpr size_t kChunkSize = 1024;
    static constexpr char kRestartMarker[] = "**** RESTART ****";
    static constexpr size_t kRestartMarkerLen = sizeof(kRestartMarker) - 1;

    enum class State {
        Start,
        ScanBackward,
        OpenTemp,
        CopyForward,
        Finalize,
        Done,
        Error,
    };

    Status fail(int statusCode, const char *reason);

    State _state = State::Start;
    int _statusCode = 204;
    std::string _errorReason;

    FsFile _src;
    FsFile _tmp;

    // Backward marker-scan state (mirrors the original chunked search).
    uint32_t _chunkEnd = 0;
    uint8_t _window[kChunkSize + kRestartMarkerLen - 1];
    uint8_t _overlap[kRestartMarkerLen - 1];
    size_t _overlapLen = 0;
    uint32_t _lastMarkerOffset = 0;
    bool _markerFound = false;
    uint32_t _startOffset = 0;

    // Forward-copy state.
    uint8_t _copyBuf[kChunkSize];
};
