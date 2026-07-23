#!/usr/bin/env python3
"""Local smoke test for the STM32 debug MCP server.

This test uses only stdio MCP calls that do not touch hardware.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "tools" / "stm32_debug_mcp" / "server.py"


def call(proc: subprocess.Popen, msg: dict) -> dict:
    assert proc.stdin is not None
    assert proc.stdout is not None
    proc.stdin.write(json.dumps(msg) + "\n")
    proc.stdin.flush()
    line = proc.stdout.readline()
    if not line:
        raise RuntimeError("server closed stdout")
    return json.loads(line)


def call_header(proc: subprocess.Popen, msg: dict) -> dict:
    assert proc.stdin is not None
    assert proc.stdout is not None
    body = json.dumps(msg).encode("utf-8")
    proc.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
    proc.stdin.write(body)
    proc.stdin.flush()
    headers = []
    while True:
        line = proc.stdout.readline()
        if line in (b"", b"\r\n", b"\n"):
            break
        headers.append(line.decode("ascii").strip())
    length = None
    for h in headers:
        if h.lower().startswith("content-length:"):
            length = int(h.split(":", 1)[1].strip())
    if length is None:
        raise RuntimeError(f"missing Content-Length in response headers: {headers}")
    data = proc.stdout.read(length)
    return json.loads(data.decode("utf-8"))


def main() -> int:
    proc = subprocess.Popen(
        [sys.executable, str(SERVER), "--root", str(ROOT)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        init = call(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        assert init["result"]["serverInfo"]["name"] == "stm32-debug-mcp"
        tools = call(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
        names = {t["name"] for t in tools["result"]["tools"]}
        assert "symbol_addr" in names
        assert "flash_jlink" in names
        assert "read_symbol_jlink" in names
        assert "write_symbol_jlink" in names
        assert "jlink_cmd" in names
        art = call(
            proc,
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {"name": "artifact_info", "arguments": {"preset": "app-b"}},
            },
        )
        assert "content" in art["result"]
        sym = call(
            proc,
            {
                "jsonrpc": "2.0",
                "id": 4,
                "method": "tools/call",
                "params": {"name": "symbol_addr", "arguments": {"preset": "app-b", "symbol": "g_mcf_spin_rc"}},
            },
        )
        assert "content" in sym["result"]
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
    proc = subprocess.Popen(
        [sys.executable, str(SERVER), "--root", str(ROOT)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        init = call_header(proc, {"jsonrpc": "2.0", "id": 10, "method": "initialize", "params": {}})
        assert init["result"]["serverInfo"]["name"] == "stm32-debug-mcp"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
    print("stm32_debug_mcp smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
