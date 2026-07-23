import os
from dataclasses import dataclass


def _required(name: str) -> str:
    value = os.environ.get(name, "").strip()
    if not value:
        raise RuntimeError(f"Missing required environment variable: {name}")
    return value


@dataclass(frozen=True)
class Settings:
    database_url: str
    admin_username: str
    admin_password: str
    session_secret: str
    mqtt_host: str
    mqtt_port: int
    mqtt_username: str
    mqtt_password: str


def load_settings() -> Settings:
    return Settings(
        database_url=_required("DATABASE_URL"),
        admin_username=os.environ.get("ADMIN_USERNAME", "admin").strip() or "admin",
        admin_password=_required("ADMIN_PASSWORD"),
        session_secret=_required("SESSION_SECRET"),
        mqtt_host=os.environ.get("MQTT_HOST", "mqtt").strip() or "mqtt",
        mqtt_port=int(os.environ.get("MQTT_PORT", "1883")),
        mqtt_username=os.environ.get("MQTT_INGEST_USERNAME", "upboard_ingest").strip() or "upboard_ingest",
        mqtt_password=_required("MQTT_INGEST_PASSWORD"),
    )

