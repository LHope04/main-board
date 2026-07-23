from __future__ import annotations

import asyncio
import json
import logging
import os
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import Cookie, FastAPI, HTTPException, Request, Response
from fastapi.responses import FileResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from . import auth, db
from .config import Settings, load_settings
from .ingest import MqttIngest


logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s %(message)s")

SETTINGS: Settings = load_settings()
LEGACY_STATIC_DIR = Path(__file__).resolve().parent / "static"
DEFAULT_FRONTEND_DIST = Path(__file__).resolve().parents[2] / "frontend" / "dist"
FRONTEND_DIST_DIR = Path(os.environ.get("FRONTEND_DIST_DIR", DEFAULT_FRONTEND_DIST))
WEB_ROOT = FRONTEND_DIST_DIR if (FRONTEND_DIST_DIR / "index.html").is_file() else LEGACY_STATIC_DIR
SESSION_COOKIE = "upboard_iot_session"
subscribers: set[asyncio.Queue[dict[str, Any]]] = set()
ingest: MqttIngest | None = None


class PumpControlRequest(BaseModel):
    duty_pct: int = Field(ge=0, le=100)


def publish_realtime(event: dict[str, Any]) -> None:
    loop = getattr(app.state, "loop", None)
    if loop is None:
        return

    def _publish() -> None:
        for queue in list(subscribers):
            try:
                queue.put_nowait(event)
            except asyncio.QueueFull:
                subscribers.discard(queue)

    loop.call_soon_threadsafe(_publish)


@asynccontextmanager
async def lifespan(app_: FastAPI):
    global ingest
    app_.state.loop = asyncio.get_running_loop()
    db.wait_for_db(SETTINGS.database_url)
    db.init_schema(SETTINGS.database_url)
    db.ensure_admin(SETTINGS.database_url, SETTINGS.admin_username, auth.hash_password(SETTINGS.admin_password))
    ingest = MqttIngest(SETTINGS, publish_realtime)
    ingest.start()
    try:
        yield
    finally:
        if ingest:
            ingest.stop()


app = FastAPI(title="Upboard IoT Admin", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=LEGACY_STATIC_DIR), name="legacy-static")
if (WEB_ROOT / "assets").is_dir():
    app.mount("/assets", StaticFiles(directory=WEB_ROOT / "assets"), name="frontend-assets")


def require_session(session: str | None = Cookie(default=None, alias=SESSION_COOKIE)) -> dict[str, Any]:
    payload = auth.verify_session(session, SETTINGS.session_secret)
    if not payload:
        raise HTTPException(status_code=401, detail="not authenticated")
    return payload


@app.get("/")
def index():
    return FileResponse(WEB_ROOT / "index.html")


@app.get("/healthz")
def healthz():
    return {"ok": True, "service": "upboard-iot"}


@app.post("/api/auth/login")
async def login(request: Request, response: Response):
    body = await request.json()
    username = str(body.get("username") or "")
    password = str(body.get("password") or "")
    user = db.get_user(SETTINGS.database_url, username)
    if not user or not auth.verify_password(password, user["password_hash"]):
        raise HTTPException(status_code=401, detail="invalid credentials")
    token = auth.make_session(user["username"], user["role"], SETTINGS.session_secret)
    response.set_cookie(
        SESSION_COOKIE,
        token,
        max_age=auth.SESSION_SECONDS,
        httponly=True,
        samesite="lax",
    )
    return {"ok": True, "username": user["username"], "role": user["role"]}


@app.post("/api/auth/logout")
def logout(response: Response):
    response.delete_cookie(SESSION_COOKIE)
    return {"ok": True}


@app.get("/api/me")
def me(session=Cookie(default=None, alias=SESSION_COOKIE)):
    payload = auth.verify_session(session, SETTINGS.session_secret)
    if not payload:
        return {"authenticated": False}
    return {"authenticated": True, "username": payload["sub"], "role": payload["role"]}


@app.get("/api/devices")
def devices(_session=Cookie(default=None, alias=SESSION_COOKIE)):
    require_session(_session)
    return {"devices": db.list_devices(SETTINGS.database_url)}


@app.get("/api/devices/{sn}/latest")
def latest(sn: str, _session=Cookie(default=None, alias=SESSION_COOKIE)):
    require_session(_session)
    return {"device": sn, "latest": db.latest_telemetry(SETTINGS.database_url, sn)}


@app.get("/api/devices/{sn}/telemetry")
def telemetry(sn: str, limit: int = 500, _session=Cookie(default=None, alias=SESSION_COOKIE)):
    require_session(_session)
    return {"device": sn, "telemetry": db.telemetry_history(SETTINGS.database_url, sn, limit)}


@app.get("/api/devices/{sn}/gps")
def gps(sn: str, limit: int = 500, _session=Cookie(default=None, alias=SESSION_COOKIE)):
    require_session(_session)
    return {"device": sn, "gps": db.gps_history(SETTINGS.database_url, sn, limit)}


@app.get("/api/devices/{sn}/events")
def events(sn: str, limit: int = 200, _session=Cookie(default=None, alias=SESSION_COOKIE)):
    require_session(_session)
    return {"device": sn, "events": db.events_history(SETTINGS.database_url, sn, limit)}


@app.post("/api/devices/{sn}/controls/pump")
def control_pump(
    sn: str,
    command: PumpControlRequest,
    _session=Cookie(default=None, alias=SESSION_COOKIE),
):
    session = require_session(_session)
    if session.get("role") != "admin":
        raise HTTPException(status_code=403, detail="admin role required")
    if not db.get_device(SETTINGS.database_url, sn):
        raise HTTPException(status_code=404, detail="device not found")
    if ingest is None:
        raise HTTPException(status_code=503, detail="MQTT service unavailable")

    topic = f"upboard/{sn}/command/pump"
    try:
        message_id = ingest.publish_json(topic, {"duty_pct": command.duty_pct}, qos=1)
    except RuntimeError as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    return {
        "ok": True,
        "device": sn,
        "topic": topic,
        "duty_pct": command.duty_pct,
        "message_id": message_id,
    }


@app.get("/api/stream")
async def stream(_session=Cookie(default=None, alias=SESSION_COOKIE)):
    require_session(_session)
    queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=100)
    subscribers.add(queue)

    async def generate():
        try:
            yield "event: hello\ndata: {}\n\n"
            while True:
                try:
                    event = await asyncio.wait_for(queue.get(), timeout=20)
                    data = json.dumps(event, separators=(",", ":"), default=str)
                    yield f"event: {event.get('type', 'message')}\ndata: {data}\n\n"
                except asyncio.TimeoutError:
                    yield "event: ping\ndata: {}\n\n"
        finally:
            subscribers.discard(queue)

    return StreamingResponse(generate(), media_type="text/event-stream")


@app.get("/{full_path:path}")
def frontend_route(full_path: str):
    if full_path == "healthz" or full_path.startswith("api/"):
        raise HTTPException(status_code=404, detail="not found")

    web_root = WEB_ROOT.resolve()
    candidate = (web_root / full_path).resolve()
    if candidate.is_relative_to(web_root) and candidate.is_file():
        return FileResponse(candidate)
    return FileResponse(web_root / "index.html")
