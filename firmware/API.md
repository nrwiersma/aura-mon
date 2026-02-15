# AuraMon HTTP API

This document describes the HTTP endpoints exposed by the firmware. All endpoints are served by the device's embedded web server over HTTP.

## Endpoint index

- [`GET /config`](#get-config)
- [`POST /config`](#post-config)
- [`GET /status`](#get-status)
- [`GET /energy`](#get-energy)
- [`POST /device/action`](#post-deviceaction)
- [`GET /logs`](#get-logs)
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
