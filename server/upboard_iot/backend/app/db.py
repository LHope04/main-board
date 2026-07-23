from __future__ import annotations

import time
from datetime import date, datetime
from typing import Any

import psycopg
from psycopg.rows import dict_row
from psycopg.types.json import Jsonb


SCHEMA = """
CREATE TABLE IF NOT EXISTS users (
    id BIGSERIAL PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'admin',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS devices (
    id BIGSERIAL PRIMARY KEY,
    sn TEXT NOT NULL UNIQUE,
    name TEXT,
    model TEXT,
    last_seen_at TIMESTAMPTZ,
    last_topic TEXT,
    last_seq BIGINT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS user_devices (
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    device_id BIGINT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    role TEXT NOT NULL DEFAULT 'owner',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (user_id, device_id)
);

CREATE TABLE IF NOT EXISTS telemetry (
    id BIGSERIAL PRIMARY KEY,
    device_id BIGINT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    sn TEXT NOT NULL,
    seq BIGINT,
    received_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    ts_device_ms BIGINT,
    rssi INTEGER,
    operator TEXT,
    ip TEXT,
    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,
    gps_fix BOOLEAN,
    ntc_raw INTEGER[],
    bat24_v DOUBLE PRECISION,
    bat24_i DOUBLE PRECISION,
    v12_v DOUBLE PRECISION,
    boost_on BOOLEAN,
    load_on BOOLEAN,
    fan_on BOOLEAN,
    pump_on BOOLEAN,
    pump_duty_pct INTEGER,
    compressor_on BOOLEAN,
    topic TEXT NOT NULL,
    raw_json JSONB NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_telemetry_device_received
    ON telemetry(device_id, received_at DESC);
CREATE INDEX IF NOT EXISTS idx_telemetry_sn_received
    ON telemetry(sn, received_at DESC);

ALTER TABLE telemetry
    ADD COLUMN IF NOT EXISTS pump_duty_pct INTEGER;

CREATE TABLE IF NOT EXISTS device_events (
    id BIGSERIAL PRIMARY KEY,
    device_id BIGINT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    sn TEXT NOT NULL,
    level TEXT NOT NULL DEFAULT 'info',
    event_type TEXT NOT NULL,
    message TEXT NOT NULL,
    received_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    topic TEXT NOT NULL,
    raw_json JSONB NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_events_device_received
    ON device_events(device_id, received_at DESC);
"""


def connect(database_url: str):
    return psycopg.connect(database_url, row_factory=dict_row)


def wait_for_db(database_url: str, timeout_s: int = 60) -> None:
    deadline = time.time() + timeout_s
    last_error: Exception | None = None
    while time.time() < deadline:
        try:
            with connect(database_url) as conn:
                with conn.cursor() as cur:
                    cur.execute("SELECT 1")
            return
        except Exception as exc:
            last_error = exc
            time.sleep(1)
    raise RuntimeError(f"PostgreSQL not ready: {last_error}")


def init_schema(database_url: str) -> None:
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            cur.execute(SCHEMA)
        conn.commit()


def ensure_admin(database_url: str, username: str, password_hash: str) -> None:
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO users (username, password_hash, role)
                VALUES (%s, %s, 'admin')
                ON CONFLICT (username) DO NOTHING
                """,
                (username, password_hash),
            )
        conn.commit()


def get_user(database_url: str, username: str) -> dict[str, Any] | None:
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            cur.execute("SELECT * FROM users WHERE username = %s", (username,))
            return cur.fetchone()


def upsert_device(cur, sn: str, topic: str, seq: int | None) -> int:
    cur.execute(
        """
        INSERT INTO devices (sn, name, last_seen_at, last_topic, last_seq)
        VALUES (%s, %s, now(), %s, %s)
        ON CONFLICT (sn) DO UPDATE
        SET last_seen_at = now(), last_topic = EXCLUDED.last_topic, last_seq = EXCLUDED.last_seq
        RETURNING id
        """,
        (sn, sn, topic, seq),
    )
    return int(cur.fetchone()["id"])


def _dig(payload: dict[str, Any], *path: str) -> Any:
    value: Any = payload
    for key in path:
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def _as_int(value: Any) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _as_float(value: Any) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _as_bool(value: Any) -> bool | None:
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(value)
    if isinstance(value, str):
        lowered = value.strip().lower()
        if lowered in {"true", "1", "yes", "on"}:
            return True
        if lowered in {"false", "0", "no", "off"}:
            return False
    return None


def _as_int_list(value: Any) -> list[int] | None:
    if not isinstance(value, list):
        return None
    out: list[int] = []
    for item in value[:16]:
        parsed = _as_int(item)
        if parsed is None:
            return None
        out.append(parsed)
    return out


def _jsonable(value: Any) -> Any:
    if isinstance(value, (datetime, date)):
        return value.isoformat()
    return value


def row_to_dict(row: dict[str, Any] | None) -> dict[str, Any] | None:
    if row is None:
        return None
    return {key: _jsonable(value) for key, value in row.items()}


def insert_telemetry(database_url: str, topic: str, topic_sn: str, payload: dict[str, Any]) -> dict[str, Any]:
    sn = str(payload.get("sn") or topic_sn).strip()
    seq = _as_int(payload.get("seq"))
    rssi = _as_int(payload.get("rssi") if payload.get("rssi") is not None else _dig(payload, "signal", "csq"))
    operator = payload.get("operator") or _dig(payload, "net", "operator")
    ip = payload.get("ip") or _dig(payload, "net", "ip")
    latitude = _as_float(payload.get("latitude") if payload.get("latitude") is not None else _dig(payload, "gps", "lat"))
    longitude = _as_float(payload.get("longitude") if payload.get("longitude") is not None else _dig(payload, "gps", "lon"))
    gps_fix = _as_bool(payload.get("gps_fix") if payload.get("gps_fix") is not None else _dig(payload, "gps", "fix"))
    ntc_raw = _as_int_list(payload.get("ntc_raw") if payload.get("ntc_raw") is not None else _dig(payload, "sampler", "ntc_raw"))
    outputs = payload.get("outputs") if isinstance(payload.get("outputs"), dict) else {}
    power = payload.get("power") if isinstance(payload.get("power"), dict) else {}

    with connect(database_url) as conn:
        with conn.cursor() as cur:
            device_id = upsert_device(cur, sn, topic, seq)
            cur.execute(
                """
                INSERT INTO telemetry (
                    device_id, sn, seq, ts_device_ms, rssi, operator, ip,
                    latitude, longitude, gps_fix, ntc_raw,
                    bat24_v, bat24_i, v12_v,
                    boost_on, load_on, fan_on, pump_on, pump_duty_pct, compressor_on,
                    topic, raw_json
                )
                VALUES (
                    %s, %s, %s, %s, %s, %s, %s,
                    %s, %s, %s, %s,
                    %s, %s, %s,
                    %s, %s, %s, %s, %s, %s,
                    %s, %s
                )
                RETURNING *
                """,
                (
                    device_id,
                    sn,
                    seq,
                    _as_int(payload.get("ts_device_ms") if payload.get("ts_device_ms") is not None else payload.get("uptime_ms")),
                    rssi,
                    str(operator) if operator is not None else None,
                    str(ip) if ip is not None else None,
                    latitude,
                    longitude,
                    gps_fix,
                    ntc_raw,
                    _as_float(power.get("bat24_v")),
                    _as_float(power.get("bat24_i")),
                    _as_float(power.get("v12_v")),
                    _as_bool(outputs.get("boost")),
                    _as_bool(outputs.get("load")),
                    _as_bool(outputs.get("fan")),
                    _as_bool(outputs.get("pump")),
                    _as_int(outputs.get("pump_duty_pct")),
                    _as_bool(outputs.get("compressor")),
                    topic,
                    Jsonb(payload),
                ),
            )
            row = cur.fetchone()
        conn.commit()
    return row_to_dict(row) or {}


def get_device(database_url: str, sn: str) -> dict[str, Any] | None:
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            cur.execute("SELECT * FROM devices WHERE sn = %s", (sn,))
            return row_to_dict(cur.fetchone())


def insert_event(database_url: str, topic: str, topic_sn: str, payload: dict[str, Any], default_type: str) -> dict[str, Any]:
    sn = str(payload.get("sn") or topic_sn).strip()
    seq = _as_int(payload.get("seq"))
    level = str(payload.get("level") or "info")
    event_type = str(payload.get("event_type") or default_type)
    message = str(payload.get("message") or payload.get("status") or event_type)
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            device_id = upsert_device(cur, sn, topic, seq)
            cur.execute(
                """
                INSERT INTO device_events (device_id, sn, level, event_type, message, topic, raw_json)
                VALUES (%s, %s, %s, %s, %s, %s, %s)
                RETURNING *
                """,
                (device_id, sn, level, event_type, message, topic, Jsonb(payload)),
            )
            row = cur.fetchone()
        conn.commit()
    return row_to_dict(row) or {}


def list_devices(database_url: str) -> list[dict[str, Any]]:
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    d.*,
                    t.received_at AS latest_received_at,
                    t.seq AS latest_seq,
                    t.rssi AS latest_rssi,
                    t.latitude AS latest_latitude,
                    t.longitude AS latest_longitude,
                    t.gps_fix AS latest_gps_fix,
                    t.raw_json AS latest_raw_json
                FROM devices d
                LEFT JOIN LATERAL (
                    SELECT *
                    FROM telemetry
                    WHERE telemetry.device_id = d.id
                    ORDER BY received_at DESC
                    LIMIT 1
                ) t ON true
                ORDER BY d.last_seen_at DESC NULLS LAST, d.created_at DESC
                """
            )
            return [row_to_dict(row) for row in cur.fetchall()]


def latest_telemetry(database_url: str, sn: str) -> dict[str, Any] | None:
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT *
                FROM telemetry
                WHERE sn = %s
                ORDER BY received_at DESC
                LIMIT 1
                """,
                (sn,),
            )
            return row_to_dict(cur.fetchone())


def telemetry_history(database_url: str, sn: str, limit: int = 500) -> list[dict[str, Any]]:
    limit = max(1, min(limit, 5000))
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT *
                FROM telemetry
                WHERE sn = %s
                ORDER BY received_at DESC
                LIMIT %s
                """,
                (sn, limit),
            )
            rows = [row_to_dict(row) for row in cur.fetchall()]
    return list(reversed(rows))


def gps_history(database_url: str, sn: str, limit: int = 500) -> list[dict[str, Any]]:
    limit = max(1, min(limit, 5000))
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT received_at, seq, latitude, longitude, gps_fix, raw_json
                FROM telemetry
                WHERE sn = %s AND latitude IS NOT NULL AND longitude IS NOT NULL
                ORDER BY received_at DESC
                LIMIT %s
                """,
                (sn, limit),
            )
            rows = [row_to_dict(row) for row in cur.fetchall()]
    return list(reversed(rows))


def events_history(database_url: str, sn: str, limit: int = 200) -> list[dict[str, Any]]:
    limit = max(1, min(limit, 1000))
    with connect(database_url) as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT *
                FROM device_events
                WHERE sn = %s
                ORDER BY received_at DESC
                LIMIT %s
                """,
                (sn, limit),
            )
            rows = [row_to_dict(row) for row in cur.fetchall()]
    return list(reversed(rows))
