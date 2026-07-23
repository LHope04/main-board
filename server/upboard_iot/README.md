# Upboard IoT Admin Platform

First-version admin-only MQTT telemetry platform for the STM32F407 upboard with EC801E.

Usage guide: [`USAGE.md`](USAGE.md)

## Services

- MQTT broker: `1883/tcp`
- Admin web/API: `18080/tcp`
- Database: PostgreSQL in Docker volume `upboard_iot_postgres_data`

The React frontend is built in the Docker multi-stage image and served by FastAPI, so no Node.js toolchain is required on the host server.

## Deploy

```bash
cp .env.example .env
python3 - <<'PY'
import secrets
for name in [
    "POSTGRES_PASSWORD",
    "ADMIN_PASSWORD",
    "SESSION_SECRET",
    "MQTT_DEVICE_PASSWORD",
    "MQTT_INGEST_PASSWORD",
]:
    print(f"{name}={secrets.token_urlsafe(24)}")
PY
```

Put the generated values into `.env`, then run:

```bash
docker compose up -d --build
docker compose ps
```

If Docker Hub is temporarily unreachable but the current `upboard-iot-web` image is already present, build the verified local frontend first, tag the running image as `upboard-iot-web:legacy`, and use `backend/Dockerfile.offline` to layer only `backend/app` and `frontend/dist`. This is a deployment fallback; the standard multi-stage Dockerfile remains the canonical clean build.

## MQTT Topics

Devices publish:

```text
upboard/{sn}/telemetry
upboard/{sn}/status
upboard/{sn}/event
```

The first version uses one MQTT device account for all boards:

```text
username = upboard_device
password = MQTT_DEVICE_PASSWORD from .env
```

Each payload must include `sn`. If it is missing, the server uses the SN segment from the topic.

## Telemetry Example

```json
{
  "sn": "UPB-DEMO-001",
  "seq": 1,
  "uptime_ms": 1000,
  "rssi": 22,
  "gps": {"lat": 22.3, "lon": 114.1, "fix": true},
  "sampler": {"ntc_raw": [1, 2, 3, 4, 5, 6, 7, 8]},
  "power": {"bat24_v": 24.1, "bat24_i": 0.3, "v12_v": 12.4},
  "outputs": {"boost": true, "load": true, "fan": false, "pump": false, "compressor": false}
}
```

## API

Admin login is required for all data APIs.

```text
POST /api/auth/login
POST /api/auth/logout
GET  /api/me
GET  /api/devices
GET  /api/devices/{sn}/latest
GET  /api/devices/{sn}/telemetry?limit=500
GET  /api/devices/{sn}/gps?limit=500
GET  /api/devices/{sn}/events?limit=200
GET  /api/stream
GET  /healthz
```

## EC801E Smoke Command Shape

After PDP is active:

```text
AT+QMTOPEN=0,"38.76.206.42",1883
AT+QMTCONN=0,"UPB-DEMO-001","upboard_device","<MQTT_DEVICE_PASSWORD>"
AT+QMTPUBEX=0,0,0,0,"upboard/UPB-DEMO-001/telemetry",56
{"sn":"UPB-DEMO-001","seq":2,"uptime_ms":2000,"rssi":22}
```

`QMTPUBEX` uses explicit payload length. After the `>` prompt, send exactly the declared JSON bytes; do not append Ctrl-Z.
