# Upboard IoT Admin MQTT Platform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first admin-only IoT platform for EC801E MQTT telemetry: broker on `1883`, SQL long-term storage, realtime dashboard, and historical queries for all devices by SN.

**Architecture:** `Mosquitto` accepts device MQTT publishes, a Python/FastAPI service subscribes to `upboard/+/telemetry|status|event`, writes PostgreSQL, and serves both API and static admin UI on `18080`. The first version has a single admin login and shows all devices; user-device binding tables are created for the later multi-user version but not exposed in the UI yet.

**Tech Stack:** Docker Compose, Eclipse Mosquitto, PostgreSQL 16, Python 3.12, FastAPI, paho-mqtt, psycopg, vanilla HTML/CSS/JS with SSE.

---

### File Structure

- Create `server/upboard_iot/docker-compose.yml`: local deployment for PostgreSQL, Mosquitto, and the web/API/ingest service.
- Create `server/upboard_iot/.env.example`: default ports and credentials template.
- Create `server/upboard_iot/mosquitto/mosquitto.conf`: MQTT listener on `1883`.
- Create `server/upboard_iot/backend/Dockerfile`: Python service container.
- Create `server/upboard_iot/backend/requirements.txt`: FastAPI, PostgreSQL, and MQTT dependencies.
- Create `server/upboard_iot/backend/app/config.py`: environment parsing.
- Create `server/upboard_iot/backend/app/db.py`: schema, SQL helpers, and payload insertion.
- Create `server/upboard_iot/backend/app/auth.py`: admin password hashing, sessions, and cookie helpers.
- Create `server/upboard_iot/backend/app/ingest.py`: MQTT subscription and database write path.
- Create `server/upboard_iot/backend/app/main.py`: FastAPI routes and SSE.
- Create `server/upboard_iot/backend/app/static/*`: admin-only dashboard.
- Create `server/upboard_iot/README.md`: deployment, MQTT topics, payload examples, and EC801E AT smoke commands.

### Task 1: Server App Skeleton

**Files:**
- Create all files under `server/upboard_iot/`

- [x] **Step 1: Add Docker Compose and Python service files**

Use service names `upboard-iot-postgres`, `upboard-iot-mqtt`, and `upboard-iot-web`. Expose host ports `1883` and `18080`.

- [x] **Step 2: Add database schema**

Create tables `users`, `devices`, `user_devices`, `telemetry`, and `device_events`. Store `received_at` as server timestamp and keep `raw_json` for forward compatibility.

- [x] **Step 3: Add MQTT ingest path**

Subscribe to:

```text
upboard/+/telemetry
upboard/+/status
upboard/+/event
```

Resolve SN from JSON `sn`, then topic SN as fallback. Auto-upsert devices by SN.

- [x] **Step 4: Add admin API**

Implement:

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

- [x] **Step 5: Add static admin dashboard**

The first screen shows login if unauthenticated, then all devices with realtime updates, latest telemetry, raw payload, recent history, and GPS points.

### Task 2: Deployment

**Files:**
- Modify only deployment files under `server/upboard_iot/`

- [x] **Step 1: Copy `server/upboard_iot/` to `/opt/upboard-iot` on `hk-newapi`**

Use `rsync --delete` to keep the remote deployment exact.

- [x] **Step 2: Create production `.env`**

Set:

```text
APP_PORT=18080
MQTT_PORT=1883
POSTGRES_PASSWORD=<generated>
ADMIN_USERNAME=admin
ADMIN_PASSWORD=<generated>
MQTT_DEVICE_USERNAME=upboard_device
MQTT_DEVICE_PASSWORD=<generated>
```

- [x] **Step 3: Start Docker Compose**

Run:

```bash
docker compose up -d --build
```

Expected: all three containers report healthy/running.

### Task 3: Verification

**Files:**
- No code changes unless a verification failure identifies a root cause.

- [x] **Step 1: Verify local server health**

Run on server:

```bash
curl -fsS http://127.0.0.1:18080/healthz
```

Expected JSON includes `"ok":true`.

- [x] **Step 2: Publish a test telemetry payload through MQTT**

Run on server inside the mosquitto container:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 \
  -t upboard/UPB-DEMO-001/telemetry \
  -u upboard_device -P "$MQTT_DEVICE_PASSWORD" \
  -m '{"sn":"UPB-DEMO-001","seq":1,"uptime_ms":1000,"rssi":22,"gps":{"lat":22.3,"lon":114.1,"fix":true},"sampler":{"ntc_raw":[1,2,3,4,5,6,7,8]},"power":{"bat24_v":24.1,"bat24_i":0.3,"v12_v":12.4},"outputs":{"boost":true,"load":true}}'
```

Expected: API `GET /api/devices` returns `UPB-DEMO-001`.

- [x] **Step 3: Verify frontend/API**

Open:

```text
http://38.76.206.42:18080/
```

Login as admin and confirm the demo device appears with latest telemetry.

- [x] **Step 4: Verify EC801E MQTT publish**

Send EC801E AT commands:

```text
AT+QMTOPEN=0,"38.76.206.42",1883
AT+QMTCONN=0,"UPB-DEMO-001","upboard_device","<MQTT_DEVICE_PASSWORD>"
AT+QMTPUBEX=0,0,0,0,"upboard/UPB-DEMO-001/telemetry",56
{"sn":"UPB-DEMO-001","seq":2,"uptime_ms":2000,"rssi":22}
```

Expected: `+QMTPUBEX: 0,0,0` and the frontend/API updates.

### Self-Review

- The first version intentionally has only admin UI and admin API access.
- The schema includes `users` and `user_devices` so the later user binding feature does not require a storage migration from scratch.
- MQTT TLS, HTTPS, user-facing permissions, command downlink, and map rendering are out of first-version scope.
