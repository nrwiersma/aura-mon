# AuraMon HTTP API

This document describes the HTTP endpoints exposed by the firmware. All endpoints are served by the device's embedded web server over HTTP.

## Endpoint index

- [`GET /config`](#get-config)
- [`POST /config`](#post-config)
- [`GET /status`](#get-status)
- [`GET /energy`](#get-energy)
- [`POST /device/action`](#post-deviceaction)
- [`GET /logs`](#get-logs)
- [`POST /logs/trunc`](#post-logstrunc)
- [`POST /ota`](#post-ota)
- [`POST /ota/public`](#post-otapublic)
- [`GET /metrics`](#get-metrics)
- [`GET /readyz`](#get-readyz)
- [`GET /livez`](#get-livez)
- [`GET /<path>` (static files)](#get-path-static-files)

## Base

- Base URL: `http://<device-ip>`
- Auth: none
- CORS: enabled

## Common responses

- `200 OK` for successful JSON/plain responses.
- `204 No Content` when there is nothing to return.
- `400 Bad Request` for invalid parameters or invalid JSON.
- `404 Not Found` when a resource does not exist.
- `405 Method Not Allowed` for disallowed methods on static files.
- `408 Request Timeout` when the SD card mutex cannot be acquired.
- `409 Conflict` when a device action is already pending.
- `500 Internal Server Error` for unexpected errors.
- `505 HTTP Version Not Supported` when chunked responses are not available.

## Endpoints

### `GET /config`

Returns the current configuration as JSON.

- Response content type: `application/json`

Example response (shape):
```json
{
  "format": 1,
  "network": {
    "hostname": "aura-mon",
    "ip": "192.168.0.0",
    "gateway": "192.168.0.1",
    "mask": "255.255.255.0",
    "dns": "8.8.8.8"
  },
  "devices": [
    {
      "enabled": true,
      "address": 1,
      "name": "test1",
      "calibration": 1.0,
      "reversed": false
    }
  ]
}
```

### `POST /config`

Updates the configuration. The request body must be JSON.

- Request content type: `application/json`
- Response content type: `text/plain`

Possible error responses:
- `{"error":"No data provided"}`
- `{"error":"Invalid JSON"}`
- `{"error":"Invalid configuration","reason":"..."}`

Example request:
```bash
curl -X POST http://<device-ip>/config \
  -H 'Content-Type: application/json' \
  -d '{"format":1,"network":{"hostname":"aura-mon"},"devices":[]}'
```

### `GET /status`

Returns runtime status and device data.

- Response content type: `application/json`

Response fields:
- `version` string.
- `stats` object: `startTime`, `currentTime`, `runSeconds`, `heapFree`.
- `devices` array: each entry has `name`, `volts`, `amps`, `pf`, `hz`.
- `datalog` object: `firstRev`, `lastRev`, `interval`.
- `network` object: `hostname`, `ip`, `gateway`, `subnet`, `dns`, `mac`.
- `uploaders` array: one entry per configured, enabled uploader (see `POST /config`'s
  `uploaders`), each with:
  - `id`, `type` - matches the uploader's config entry.
  - `state` - `"build"`, `"post"`, or `"wait"`; which step of its request cycle it's
    currently in.
  - `lastSentTS` - datalog timestamp up to which data has been successfully uploaded.
  - `lagSeconds` - how far `lastSentTS` is behind the datalog's most recent record; grows
    while the network is down or the remote endpoint is failing, and shrinks once it
    catches back up.
  - `successTotal`, `failureTotal` - lifetime (since boot) counts of completed requests.
  - `consecutiveFailures` - resets to 0 on the next success; useful for alerting on a
    stuck/misconfigured uploader without tripping on an isolated blip.
  - `lastSuccessAt`, `lastAttemptAt` - unix timestamps, `0` if never.
  - `lastHttpStatus` - HTTP status of the last completed request; `-1` if the last attempt
    failed before a response was received (e.g. could not open/send), `0` if no request has
    completed yet.

### `GET /energy`

Returns energy data as CSV in a chunked response.

- Response content type: `text/plain` (CSV)

Query parameters:
- `start` (required): unix timestamp (seconds).
- `end` (optional): unix timestamp (seconds), defaults to `now`.
- `interval` (optional): seconds, defaults to `5`.

Behavior:
- `start`, `end`, and `interval` are rounded down to the nearest datalog interval.
- If `start >= end` or `interval == 0`, returns `400`.
- Response is capped to 100 rows (`end = start + interval * 100`).
- Returns `204` if there is no data, no enabled devices, or `start` is beyond the last timestamp.

CSV columns:
- `timestamp`
- `Hz`
- For each enabled device: `<name>.V`, `<name>.A`, `<name>.W`, `<name>.Wh`, `<name>.PF`

Example:
```bash
curl "http://<device-ip>/energy?start=1730000000&end=1730003600&interval=60"
```

### `POST /device/action`

Queues a device action.

- Request content type: `application/json`
- Response content type: `application/json`

Request body:
```json
{
  "action": "locate" | "assign",
  "address": 1
}
```

Notes:
- `address` must be in `1..15`.
- Returns `202` with `{"status":"queued"}` when accepted.
- Returns `409` if another action is already pending.

### `GET /logs`

Streams the message log file from the SD card.

- Response content type: `text/plain`
- Query parameters:
  - `start` (optional): byte offset to start streaming from.
  - `limit` (optional): maximum number of bytes to stream.
- Returns `204` when `start` is at or beyond the end of the file, or when `limit=0`.
- Returns `400` for an invalid `start` or `limit` value.
- Returns `404` if the log file is missing.

Example:
```bash
curl "http://<device-ip>/logs?start=1024&limit=4096"
```

### `POST /logs/trunc`

Truncates the message log by keeping content from the last `**** RESTART ****` marker to the end of the file.

- Response content type: `text/plain`
- Uses a temp file and replace flow (`temp -> log`) so truncation is resilient to partial writes.
- Returns `204` on success.
- Returns `404` if the log file is missing.
- Returns `408` if the SD card mutex cannot be acquired.
- Returns `500` for file IO/rename failures.

Example:
```bash
curl -X POST http://<device-ip>/logs/trunc
```

### `POST /ota`

Firmware update via multipart upload.

- Request content type: `multipart/form-data`
- Form field name: `firmware`

Responses:
- `204` on success (device reboots).
- `500` on failure with `{"error":"Update failed","code":<code>}`.

Example:
```bash
curl -X POST http://<device-ip>/ota \
  -F 'firmware=@firmware.bin'
```

### `POST /ota/public`

Upload a static file to the SD card `public/` directory.

- Request content type: `multipart/form-data`
- Form field name: `file`
- Filename must not be empty and must not contain `/` or `\\`.

Responses:
- `204` on success.
- `400` on invalid field name or filename.
- `408` if the SD card mutex cannot be acquired.
- `500` on write failure.

Example:
```bash
curl -X POST http://<device-ip>/ota/public \
  -F 'file=@index.html'
```

### `GET /metrics`

Prometheus-style metrics in text format.

- Response content type: `text/plain`

Exposed metrics:
- `auramon_modbus_errors_total` (counter)
- `auramon_collect_time_seconds_total` (counter)
- `auramon_collect_time_seconds_avg` (gauge)
- `auramon_datalog_read_io` (counter)
- `auramon_datalog_write_io` (counter)
- `auramon_datalog_write_time_seconds_total` (counter)
- `auramon_datalog_cache_hit` (counter)
- `auramon_datalog_write_errors_total` (counter)
- `auramon_datalog_queue_depth` (gauge)
- `auramon_datalog_queue_full_total` (counter)
- `auramon_datalog_records_dropped_total` (counter)
- `auramon_uploader_requests_total{id,type}` (counter) - total successful upload requests.
- `auramon_uploader_errors_total{id,type}` (counter) - total failed upload attempts (network
  or non-2xx response).
- `auramon_uploader_consecutive_failures{id,type}` (gauge) - current run of consecutive
  failed attempts, reset on the next success.
- `auramon_uploader_lag_seconds{id,type}` (gauge) - how far behind the uploader is from the
  most recently logged record.
- `auramon_uploader_last_success_timestamp_seconds{id,type}` (gauge) - unix timestamp of the
  last successful upload, `0` if never.
- `auramon_uploader_last_http_status{id,type}` (gauge) - HTTP status of the last completed
  request; `-1` if it failed before a response was received, `0` if none has completed yet.

### `GET /readyz`

Returns `200` with an empty body.

### `GET /livez`

Returns `200` with an empty body.

### `GET /<path>` (static files)

Any other `GET` request serves files from the SD card `public/` directory.

Behavior:
- `/` maps to `/index.html`.
- If a `.gz` version exists, it is served with `Content-Encoding: gzip`.
- Directories return `403`.
- Unknown paths return `404`.

Common content types:
- `.html` -> `text/html`
- `.css` -> `text/css`
- `.js` -> `application/javascript`
- `.json` -> `application/json`
- `.png` -> `image/png`
- `.jpg`/`.jpeg` -> `image/jpeg`
- `.svg` -> `image/svg+xml`
