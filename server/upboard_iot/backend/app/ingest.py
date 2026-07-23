from __future__ import annotations

import json
import logging
import threading
from collections.abc import Callable
from typing import Any

import paho.mqtt.client as mqtt

from . import db
from .config import Settings


LOGGER = logging.getLogger("upboard.iot.ingest")


class MqttIngest:
    def __init__(self, settings: Settings, on_event: Callable[[dict[str, Any]], None]):
        self.settings = settings
        self.on_event = on_event
        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="upboard-iot-ingest")
        self._client.username_pw_set(settings.mqtt_username, settings.mqtt_password)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message
        self._started = threading.Event()
        self._connected = threading.Event()

    def start(self) -> None:
        self._client.connect_async(self.settings.mqtt_host, self.settings.mqtt_port, keepalive=60)
        self._client.loop_start()
        self._started.set()
        LOGGER.info("MQTT ingest connecting to %s:%s", self.settings.mqtt_host, self.settings.mqtt_port)

    def stop(self) -> None:
        if self._started.is_set():
            self._client.loop_stop()
            self._client.disconnect()

    def _on_connect(self, client, userdata, flags, reason_code, properties) -> None:
        LOGGER.info("MQTT connected: %s", reason_code)
        if reason_code != 0:
            self._connected.clear()
            return
        self._connected.set()
        client.subscribe("upboard/+/telemetry", qos=0)
        client.subscribe("upboard/+/status", qos=0)
        client.subscribe("upboard/+/event", qos=0)

    def _on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties) -> None:
        self._connected.clear()
        LOGGER.warning("MQTT disconnected: %s", reason_code)

    def publish_json(self, topic: str, payload: dict[str, Any], qos: int = 1) -> int:
        if not self._connected.wait(timeout=2.0):
            raise RuntimeError("MQTT broker is not connected")
        encoded = json.dumps(payload, separators=(",", ":"), ensure_ascii=True)
        info = self._client.publish(topic, encoded, qos=qos, retain=False)
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"MQTT publish failed with rc={info.rc}")
        info.wait_for_publish(timeout=3.0)
        if not info.is_published():
            raise RuntimeError("MQTT publish acknowledgement timed out")
        return int(info.mid)

    def _on_message(self, client, userdata, msg) -> None:
        topic = msg.topic
        parts = topic.split("/")
        topic_sn = parts[1] if len(parts) >= 3 else "unknown"
        kind = parts[2] if len(parts) >= 3 else "event"
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
            if not isinstance(payload, dict):
                payload = {"value": payload}
        except Exception as exc:
            LOGGER.warning("Invalid JSON on %s: %s", topic, exc)
            payload = {"sn": topic_sn, "level": "error", "event_type": "invalid_json", "message": msg.payload.decode("utf-8", "replace")}
            kind = "event"

        try:
            if kind == "telemetry":
                row = db.insert_telemetry(self.settings.database_url, topic, topic_sn, payload)
                event = {"type": "telemetry", "sn": row.get("sn", topic_sn), "data": row}
            else:
                row = db.insert_event(self.settings.database_url, topic, topic_sn, payload, kind)
                event = {"type": "event", "sn": row.get("sn", topic_sn), "data": row}
            self.on_event(event)
        except Exception:
            LOGGER.exception("Failed to store MQTT message from %s", topic)
