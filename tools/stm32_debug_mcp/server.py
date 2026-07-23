#!/usr/bin/env python3
"""STM32 debug MCP server for the upboard project.

This server deliberately works from symbols in the current ELF instead of
accepting raw RAM addresses from the agent. That keeps SWD reads tied to the
latest build and avoids stale nm/map addresses after BSS layout changes.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


SERVER_NAME = "stm32-debug-mcp"
SERVER_VERSION = "0.1.0"

PRESET_ARTIFACTS = {
    "bootloader": ("bootloader/bootloader.elf", "bootloader/bootloader.hex", "bootloader/bootloader.bin"),
    "app-a": ("app/upboard_A.elf", "app/upboard_A.hex", "app/upboard_A.bin"),
    "app-b": ("app/upboard_B.elf", "app/upboard_B.hex", "app/upboard_B.bin"),
    "bootloader-debug": ("bootloader/bootloader.elf", "bootloader/bootloader.hex", "bootloader/bootloader.bin"),
    "app-a-debug": ("app/upboard_A.elf", "app/upboard_A.hex", "app/upboard_A.bin"),
    "app-b-debug": ("app/upboard_B.elf", "app/upboard_B.hex", "app/upboard_B.bin"),
}

PRESET_FLASH_BASES = {
    "bootloader": 0x08000000,
    "bootloader-debug": 0x08000000,
    "app-a": 0x08020000,
    "app-a-debug": 0x08020000,
    "app-b": 0x08040000,
    "app-b-debug": 0x08040000,
}

MCF_SYMBOLS = [
    "g_mcf_scan_addr",
    "g_mcf_w_ok",
    "g_mcf_w_err",
    "g_mcf_r_ok",
    "g_mcf_r_err",
    "g_mcf_last_err",
    "g_mcf_kick_rc",
    "g_mcf_spin_rc",
    "g_mcf_status_rc",
    "g_mcf_state_rc",
    "g_mcf_i2c_disable",
    "g_mcf_i2c_recover_cnt",
    "g_mcf_algo_state",
    "g_mcf_algo_status",
    "g_mcf_ctrl_fault",
    "g_mcf_gate_fault",
    "g_mcf_spin_duty",
    "g_compressor_on",
    "g_mcf_fg_speed",
    "g_mcf_speed_fdbk",
    "g_mcf_vm_voltage",
    "g_mcf_phase_a",
]


class DebugError(Exception):
    pass


class Stm32DebugServer:
    def __init__(self, root: Path):
        self.root = root.resolve()

    def _run(self, argv: List[str], timeout: float = 30.0) -> subprocess.CompletedProcess:
        try:
            return subprocess.run(
                argv,
                cwd=str(self.root),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=timeout,
            )
        except FileNotFoundError as exc:
            raise DebugError(f"command not found: {argv[0]}") from exc
        except subprocess.TimeoutExpired as exc:
            raise DebugError(f"command timed out after {timeout}s: {shlex.join(argv)}") from exc

    def _require_tool(self, name: str) -> str:
        path = shutil.which(name)
        if not path:
            raise DebugError(f"required tool not found in PATH: {name}")
        return path

    def _preset_paths(self, preset: str) -> Dict[str, Path]:
        base = self.root / "build" / preset
        if preset not in PRESET_ARTIFACTS:
            return {
                "build_dir": base,
                "elf": self._discover_artifact(base, "elf") or (base / f"{preset}.elf"),
                "hex": self._discover_artifact(base, "hex") or (base / f"{preset}.hex"),
                "bin": self._discover_artifact(base, "bin") or (base / f"{preset}.bin"),
            }
        elf_rel, hex_rel, bin_rel = PRESET_ARTIFACTS[preset]
        return {
            "build_dir": base,
            "elf": base / elf_rel,
            "hex": base / hex_rel,
            "bin": base / bin_rel,
        }

    @staticmethod
    def _discover_artifact(base: Path, ext: str) -> Optional[Path]:
        if not base.exists():
            return None
        matches = sorted(p for p in base.rglob(f"*.{ext}") if "CMakeFiles" not in p.parts)
        if not matches:
            return None
        if len(matches) == 1:
            return matches[0]
        newest = max(matches, key=lambda p: p.stat().st_mtime)
        newest_mtime = newest.stat().st_mtime
        ties = [p for p in matches if p.stat().st_mtime == newest_mtime]
        if len(ties) == 1:
            return newest
        sample = ", ".join(str(p) for p in matches[:8])
        raise DebugError(
            f"multiple .{ext} artifacts found under {base}; pass artifact_path explicitly. matches: {sample}"
        )

    def _elf_path(self, preset: str) -> Path:
        paths = self._preset_paths(preset)
        elf = paths["elf"]
        if not elf.exists():
            raise DebugError(f"ELF not found for preset {preset}: {elf}; run build first")
        return elf

    def _symbol_addr(self, preset: str, symbol: str) -> Tuple[int, str, str]:
        self._require_tool("arm-none-eabi-nm")
        elf = self._elf_path(preset)
        cp = self._run(["arm-none-eabi-nm", "-S", "--defined-only", str(elf)], timeout=20.0)
        if cp.returncode != 0:
            raise DebugError(cp.stderr.strip() or cp.stdout.strip() or "arm-none-eabi-nm failed")
        matches = []
        for line in cp.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 4 and parts[-1] == symbol:
                addr_s, size_s, kind = parts[0], parts[1], parts[2]
                matches.append((int(addr_s, 16), kind, size_s))
            elif len(parts) >= 3 and parts[-1] == symbol:
                addr_s, kind = parts[0], parts[1]
                matches.append((int(addr_s, 16), kind, ""))
        if not matches:
            raise DebugError(f"symbol {symbol!r} not found in {elf}")
        if len(matches) > 1:
            raise DebugError(f"symbol {symbol!r} is ambiguous in {elf}: {matches}")
        return matches[0]

    def _openocd(self, commands: Iterable[str], timeout: float = 20.0) -> str:
        self._require_tool("openocd")
        argv = ["openocd", "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg"]
        for cmd in commands:
            argv.extend(["-c", cmd])
        cp = self._run(argv, timeout=timeout)
        output = (cp.stdout or "") + (cp.stderr or "")
        if cp.returncode != 0:
            raise DebugError(output.strip() or "openocd failed")
        return output

    def _jlink_tool(self) -> str:
        return (
            os.environ.get("STM32_DEBUG_MCP_JLINK_EXE")
            or shutil.which("JLinkExe")
            or shutil.which("JLink")
            or self._require_tool("JLinkExe")
        )

    def _jlink_defaults(self, args: Dict[str, Any]) -> Tuple[str, str, str]:
        device = str(
            args.get("device")
            or os.environ.get("STM32_DEBUG_MCP_JLINK_DEVICE")
            or os.environ.get("JLINK_DEVICE")
            or "STM32F407VE"
        )
        interface = str(
            args.get("interface")
            or os.environ.get("STM32_DEBUG_MCP_JLINK_INTERFACE")
            or os.environ.get("JLINK_INTERFACE")
            or "SWD"
        )
        speed = str(
            args.get("speed")
            or os.environ.get("STM32_DEBUG_MCP_JLINK_SPEED")
            or os.environ.get("JLINK_SPEED")
            or "4000"
        )
        return device, interface, speed

    def _jlink(self, commands: Iterable[str], args: Dict[str, Any], timeout: float = 30.0) -> str:
        jlink = self._jlink_tool()
        device, interface, speed = self._jlink_defaults(args)
        script_lines = [
            f"device {device}",
            f"si {interface}",
            f"speed {speed}",
            "connect",
        ]
        script_lines.extend(commands)
        if not script_lines[-1].strip().lower().startswith("q"):
            script_lines.append("q")
        script = "\n".join(script_lines) + "\n"
        with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as fh:
            fh.write(script)
            script_path = fh.name
        try:
            cp = self._run([jlink, "-NoGui", "1", "-CommanderScript", script_path], timeout=timeout)
        finally:
            try:
                os.unlink(script_path)
            except OSError:
                pass
        output = (cp.stdout or "") + (cp.stderr or "")
        bad_patterns = (
            "Cannot connect",
            "Can not connect",
            "FAILED",
            "Failed to",
            "ERROR:",
            "Error:",
            "Unknown command",
        )
        if cp.returncode != 0 or any(p in output for p in bad_patterns):
            raise DebugError(output.strip() or "J-Link command failed")
        return output

    @staticmethod
    def _parse_mdw(output: str) -> List[int]:
        vals: List[int] = []
        for line in output.splitlines():
            # OpenOCD mdw line example: 0x20000000: 00000001 00000002
            m = re.search(r"0x[0-9a-fA-F]+:\s*(.*)$", line)
            if not m:
                continue
            for token in m.group(1).split():
                if re.fullmatch(r"[0-9a-fA-F]{8}", token):
                    vals.append(int(token, 16))
        if not vals:
            raise DebugError(f"no mdw values found in OpenOCD output:\n{output}")
        return vals

    @staticmethod
    def _parse_mdb(output: str) -> List[int]:
        vals: List[int] = []
        for line in output.splitlines():
            # OpenOCD mdb line example: 0x20000000: 01 02 03 04
            m = re.search(r"0x[0-9a-fA-F]+:\s*(.*)$", line)
            if not m:
                continue
            for token in m.group(1).split():
                if re.fullmatch(r"[0-9a-fA-F]{2}", token):
                    vals.append(int(token, 16))
        if not vals:
            raise DebugError(f"no mdb values found in OpenOCD output:\n{output}")
        return vals

    @staticmethod
    def _parse_openocd_memory_by_addr(output: str) -> Dict[int, List[int]]:
        out: Dict[int, List[int]] = {}
        for line in output.splitlines():
            m = re.search(r"(0x[0-9a-fA-F]+):\s*(.*)$", line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            vals: List[int] = []
            for token in m.group(2).split():
                if re.fullmatch(r"[0-9a-fA-F]{8}", token):
                    vals.append(int(token, 16))
                elif re.fullmatch(r"[0-9a-fA-F]{2}", token):
                    vals.append(int(token, 16))
            if vals:
                out[addr] = vals
        return out

    @staticmethod
    def _parse_jlink_memory(output: str) -> List[int]:
        vals: List[int] = []
        for line in output.splitlines():
            # J-Link Commander common forms:
            # 20000000 = 00000001
            # 20000000 = 01 02 03 04
            m = re.search(r"\b[0-9a-fA-F]{8}\s*=\s*(.*)$", line)
            if not m:
                continue
            for token in m.group(1).split():
                token = token.strip(",")
                if re.fullmatch(r"[0-9a-fA-F]{8}", token):
                    vals.append(int(token, 16))
                elif re.fullmatch(r"[0-9a-fA-F]{2}", token):
                    vals.append(int(token, 16))
        if not vals:
            raise DebugError(f"no memory values found in J-Link output:\n{output}")
        return vals

    @staticmethod
    def _word_count(byte_count: Optional[int], default_words: int = 1) -> int:
        if byte_count is None:
            return default_words
        if byte_count <= 0:
            raise DebugError("byte_count must be positive")
        return (byte_count + 3) // 4

    def tool_build(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset = str(args.get("preset", "app-b"))
        cp = self._run(["cmake", "--build", "--preset", preset], timeout=float(args.get("timeout_s", 120)))
        ok = cp.returncode == 0
        return {
            "ok": ok,
            "preset": preset,
            "stdout": cp.stdout,
            "stderr": cp.stderr,
            "returncode": cp.returncode,
        }

    def tool_artifact_info(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset = str(args.get("preset", "app-b"))
        paths = self._preset_paths(preset)
        out: Dict[str, Any] = {"preset": preset, "artifacts": {}}
        for key in ("elf", "hex", "bin"):
            p = paths[key]
            out["artifacts"][key] = {
                "path": str(p),
                "exists": p.exists(),
                "size": p.stat().st_size if p.exists() else None,
                "mtime": p.stat().st_mtime if p.exists() else None,
            }
        return out

    def tool_symbol_addr(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset = str(args.get("preset", "app-b"))
        symbol = str(args["symbol"])
        addr, kind, size = self._symbol_addr(preset, symbol)
        return {
            "preset": preset,
            "symbol": symbol,
            "address": addr,
            "address_hex": f"0x{addr:08X}",
            "kind": kind,
            "size_hex": size,
            "elf": str(self._elf_path(preset)),
        }

    def tool_read_symbol_openocd(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset = str(args.get("preset", "app-b"))
        symbol = str(args["symbol"])
        byte_count = args.get("byte_count")
        addr, kind, size_s = self._symbol_addr(preset, symbol)
        size = 0
        if byte_count is None and size_s:
            try:
                size = int(size_s, 16)
                byte_count = size
            except ValueError:
                byte_count = None
        halt = bool(args.get("halt", True))
        resume = bool(args.get("resume", False))
        cmds = ["init"]
        if halt:
            cmds.append("halt")
        if byte_count == 1 or size == 1:
            cmds.append(f"mdb 0x{addr:08X} 1")
            parser = self._parse_mdb
        else:
            words = self._word_count(byte_count, 1)
            cmds.append(f"mdw 0x{addr:08X} {words}")
            parser = self._parse_mdw
        if resume:
            cmds.append("resume")
        cmds.append("exit")
        output = self._openocd(cmds, timeout=20.0)
        vals = parser(output)
        return {
            "preset": preset,
            "symbol": symbol,
            "address_hex": f"0x{addr:08X}",
            "kind": kind,
            "halted": halt,
            "resumed": resume,
            "values": vals,
            "values_hex": [f"0x{v:08X}" if v > 0xFF else f"0x{v:02X}" for v in vals],
            "openocd_output": output,
        }

    def tool_write_symbol_openocd(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset = str(args.get("preset", "app-b"))
        symbol = str(args["symbol"])
        value = int(str(args["value"]), 0) if isinstance(args["value"], str) else int(args["value"])
        addr, kind, size_s = self._symbol_addr(preset, symbol)
        if size_s and int(size_s, 16) > 4 and not args.get("allow_large_symbol"):
            raise DebugError(
                f"{symbol} size is {size_s} bytes; refusing single-word write without allow_large_symbol=true"
            )
        halt = bool(args.get("halt", True))
        resume = bool(args.get("resume", False))
        cmds = ["init"]
        if halt:
            cmds.append("halt")
        cmds.append(f"mww 0x{addr:08X} 0x{value & 0xFFFFFFFF:08X}")
        if resume:
            cmds.append("resume")
        cmds.append("exit")
        output = self._openocd(cmds, timeout=20.0)
        return {
            "preset": preset,
            "symbol": symbol,
            "address_hex": f"0x{addr:08X}",
            "value_hex": f"0x{value & 0xFFFFFFFF:08X}",
            "halted": halt,
            "resumed": resume,
            "openocd_output": output,
        }

    def tool_mcf_snapshot_openocd(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset = str(args.get("preset", "app-b"))
        snapshot: Dict[str, Any] = {}
        errors: Dict[str, str] = {}
        cmds = ["init"]
        halt = bool(args.get("halt", True))
        resume = bool(args.get("resume", False))
        if halt:
            cmds.append("halt")
        symbols: List[Tuple[str, int, int]] = []
        for sym in MCF_SYMBOLS:
            try:
                addr, kind, size_s = self._symbol_addr(preset, sym)
                size = int(size_s, 16) if size_s else 4
                symbols.append((sym, addr, size))
                if size == 1:
                    cmds.append(f"mdb 0x{addr:08X} 1")
                else:
                    cmds.append(f"mdw 0x{addr:08X} 1")
            except Exception as exc:  # keep collecting the rest
                errors[sym] = str(exc)
        if resume:
            cmds.append("resume")
        cmds.append("exit")
        output = self._openocd(cmds, timeout=float(args.get("timeout_s", 30)))
        by_addr = self._parse_openocd_memory_by_addr(output)
        for sym, addr, size in symbols:
            vals = by_addr.get(addr)
            if not vals:
                errors[sym] = f"no value returned for 0x{addr:08X}"
                continue
            val = vals[0]
            snapshot[sym] = {
                "address_hex": f"0x{addr:08X}",
                "value": val,
                "value_hex": f"0x{val:02X}" if size == 1 else f"0x{val:08X}",
                "size": size,
            }
        return {
            "preset": preset,
            "elf": str(self._elf_path(preset)),
            "halted": halt,
            "resumed": resume,
            "snapshot": snapshot,
            "errors": errors,
            "openocd_output": output,
        }

    def tool_openocd_cmd(self, args: Dict[str, Any]) -> Dict[str, Any]:
        raw = str(args["cmd"])
        if not args.get("allow_raw"):
            allowed = ("mdw ", "mdb ", "mww ", "reset ", "halt", "resume", "reg", "targets")
            if not raw.startswith(allowed):
                raise DebugError("raw OpenOCD command refused; pass allow_raw=true for non-read/debug commands")
        output = self._openocd(["init", raw, "exit"], timeout=float(args.get("timeout_s", 20)))
        return {"cmd": raw, "openocd_output": output}

    def tool_flash_openocd(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset = str(args.get("preset", "app-b"))
        artifact = str(args.get("artifact", "hex"))
        reset = bool(args.get("reset", True))
        paths = self._preset_paths(preset)
        if artifact not in ("hex", "bin", "elf"):
            raise DebugError("artifact must be one of: hex, bin, elf")
        path = paths[artifact]
        if not path.exists():
            raise DebugError(f"artifact not found: {path}")
        cmds = ["init", "reset halt", f"program {str(path)} verify"]
        if reset:
            cmds.append("reset run")
        cmds.append("exit")
        output = self._openocd(cmds, timeout=float(args.get("timeout_s", 120)))
        return {"preset": preset, "artifact": str(path), "reset": reset, "openocd_output": output}

    def _artifact_from_args(self, args: Dict[str, Any]) -> Tuple[str, str, Path]:
        preset = str(args.get("preset", "app-b"))
        artifact = str(args.get("artifact", "hex"))
        if "artifact_path" in args:
            path = Path(str(args["artifact_path"]))
            if not path.is_absolute():
                path = self.root / path
            if not path.exists():
                raise DebugError(f"artifact_path not found: {path}")
            return preset, path.suffix.lstrip(".").lower(), path
        paths = self._preset_paths(preset)
        if artifact not in ("hex", "bin", "elf"):
            raise DebugError("artifact must be one of: hex, bin, elf")
        path = paths[artifact]
        if not path.exists():
            raise DebugError(f"artifact not found: {path}")
        return preset, artifact, path

    def tool_jlink_cmd(self, args: Dict[str, Any]) -> Dict[str, Any]:
        raw_cmds = args.get("commands", args.get("cmd"))
        if raw_cmds is None:
            raise DebugError("jlink_cmd requires cmd or commands")
        if isinstance(raw_cmds, str):
            commands = [line.strip() for line in raw_cmds.splitlines() if line.strip()]
        else:
            commands = [str(x).strip() for x in raw_cmds if str(x).strip()]
        if not args.get("allow_raw"):
            allowed = ("h", "halt", "g", "go", "r", "reset", "mem", "mem8", "mem16", "mem32", "w1", "w2", "w4", "regs")
            for cmd in commands:
                if not cmd.lower().startswith(allowed):
                    raise DebugError("raw J-Link command refused; pass allow_raw=true for non-debug commands")
        output = self._jlink(commands, args, timeout=float(args.get("timeout_s", 30)))
        return {
            "commands": commands,
            "device": self._jlink_defaults(args)[0],
            "interface": self._jlink_defaults(args)[1],
            "speed": self._jlink_defaults(args)[2],
            "jlink_output": output,
        }

    def tool_flash_jlink(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset, artifact, path = self._artifact_from_args(args)
        reset = bool(args.get("reset", True))
        commands = ["r", "h"]
        if artifact == "bin":
            address = args.get("address", args.get("base_address", PRESET_FLASH_BASES.get(preset)))
            if address is None:
                raise DebugError("J-Link loadfile for .bin requires address/base_address for this preset")
            addr = int(str(address), 0) if isinstance(address, str) else int(address)
            commands.append(f"loadfile {path} 0x{addr:08X}")
            commands.append(f"verifybin {path} 0x{addr:08X}")
        else:
            commands.append(f"loadfile {path}")
        if reset:
            commands.extend(["r", "g"])
        output = self._jlink(commands, args, timeout=float(args.get("timeout_s", 120)))
        verified = bool(re.search(r"(O\.K\.|Verified|verify.*success|Flash download.*finished)", output, re.I))
        return {
            "preset": preset,
            "artifact": str(path),
            "artifact_type": artifact,
            "reset": reset,
            "verified_hint": verified,
            "device": self._jlink_defaults(args)[0],
            "interface": self._jlink_defaults(args)[1],
            "speed": self._jlink_defaults(args)[2],
            "jlink_output": output,
        }

    def tool_read_symbol_jlink(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset = str(args.get("preset", "app-b"))
        symbol = str(args["symbol"])
        byte_count = args.get("byte_count")
        addr, kind, size_s = self._symbol_addr(preset, symbol)
        size = 0
        if byte_count is None and size_s:
            try:
                size = int(size_s, 16)
                byte_count = size
            except ValueError:
                byte_count = None
        halt = bool(args.get("halt", True))
        resume = bool(args.get("resume", False))
        commands: List[str] = []
        if halt:
            commands.append("h")
        if byte_count == 1 or size == 1:
            commands.append(f"mem8 0x{addr:08X} 1")
        else:
            words = self._word_count(byte_count, 1)
            commands.append(f"mem32 0x{addr:08X} {words}")
        if resume:
            commands.append("g")
        output = self._jlink(commands, args, timeout=float(args.get("timeout_s", 30)))
        vals = self._parse_jlink_memory(output)
        return {
            "preset": preset,
            "symbol": symbol,
            "address_hex": f"0x{addr:08X}",
            "kind": kind,
            "halted": halt,
            "resumed": resume,
            "values": vals,
            "values_hex": [f"0x{v:08X}" if v > 0xFF else f"0x{v:02X}" for v in vals],
            "device": self._jlink_defaults(args)[0],
            "interface": self._jlink_defaults(args)[1],
            "speed": self._jlink_defaults(args)[2],
            "jlink_output": output,
        }

    def tool_write_symbol_jlink(self, args: Dict[str, Any]) -> Dict[str, Any]:
        preset = str(args.get("preset", "app-b"))
        symbol = str(args["symbol"])
        value = int(str(args["value"]), 0) if isinstance(args["value"], str) else int(args["value"])
        addr, kind, size_s = self._symbol_addr(preset, symbol)
        if size_s and int(size_s, 16) > 4 and not args.get("allow_large_symbol"):
            raise DebugError(
                f"{symbol} size is {size_s} bytes; refusing single-word write without allow_large_symbol=true"
            )
        halt = bool(args.get("halt", True))
        resume = bool(args.get("resume", False))
        commands: List[str] = []
        if halt:
            commands.append("h")
        commands.append(f"w4 0x{addr:08X} 0x{value & 0xFFFFFFFF:08X}")
        if resume:
            commands.append("g")
        output = self._jlink(commands, args, timeout=float(args.get("timeout_s", 30)))
        return {
            "preset": preset,
            "symbol": symbol,
            "address_hex": f"0x{addr:08X}",
            "value_hex": f"0x{value & 0xFFFFFFFF:08X}",
            "halted": halt,
            "resumed": resume,
            "device": self._jlink_defaults(args)[0],
            "interface": self._jlink_defaults(args)[1],
            "speed": self._jlink_defaults(args)[2],
            "jlink_output": output,
        }

    def tool_serial_list(self, args: Dict[str, Any]) -> Dict[str, Any]:
        ports = sorted(
            glob.glob("/dev/tty.usb*")
            + glob.glob("/dev/cu.usb*")
            + glob.glob("/dev/tty.SLAB*")
            + glob.glob("/dev/cu.SLAB*")
            + glob.glob("/dev/tty.wch*")
            + glob.glob("/dev/cu.wch*")
            + glob.glob("/dev/ttyACM*")
            + glob.glob("/dev/ttyUSB*")
        )
        return {"ports": ports}

    def tool_serial_capture(self, args: Dict[str, Any]) -> Dict[str, Any]:
        port = str(args["port"])
        baud = int(args.get("baud", 115200))
        duration_s = float(args.get("duration_s", 3.0))
        try:
            import serial  # type: ignore
        except Exception as exc:
            raise DebugError("pyserial is required for serial_capture; install pyserial in this Python env") from exc
        deadline = time.time() + duration_s
        chunks: List[bytes] = []
        with serial.Serial(port, baudrate=baud, timeout=0.1) as ser:
            while time.time() < deadline:
                data = ser.read(4096)
                if data:
                    chunks.append(data)
        blob = b"".join(chunks)
        return {
            "port": port,
            "baud": baud,
            "duration_s": duration_s,
            "bytes": len(blob),
            "text": blob.decode("utf-8", errors="replace"),
            "hex": blob.hex(" "),
        }

    def call_tool(self, name: str, arguments: Dict[str, Any]) -> Dict[str, Any]:
        table = {
            "build": self.tool_build,
            "artifact_info": self.tool_artifact_info,
            "symbol_addr": self.tool_symbol_addr,
            "read_symbol_openocd": self.tool_read_symbol_openocd,
            "write_symbol_openocd": self.tool_write_symbol_openocd,
            "mcf_snapshot_openocd": self.tool_mcf_snapshot_openocd,
            "openocd_cmd": self.tool_openocd_cmd,
            "flash_openocd": self.tool_flash_openocd,
            "jlink_cmd": self.tool_jlink_cmd,
            "flash_jlink": self.tool_flash_jlink,
            "read_symbol_jlink": self.tool_read_symbol_jlink,
            "write_symbol_jlink": self.tool_write_symbol_jlink,
            "serial_list": self.tool_serial_list,
            "serial_capture": self.tool_serial_capture,
        }
        if name not in table:
            raise DebugError(f"unknown tool: {name}")
        return table[name](arguments or {})


def tool_schema() -> List[Dict[str, Any]]:
    def obj(props: Dict[str, Any], required: Optional[List[str]] = None) -> Dict[str, Any]:
        return {"type": "object", "properties": props, "required": required or []}

    preset_prop = {"type": "string", "description": "CMake preset, e.g. app-a/app-b/bootloader"}
    jlink_props = {
        "device": {"type": "string", "description": "J-Link device name; defaults env STM32_DEBUG_MCP_JLINK_DEVICE/JLINK_DEVICE or STM32F407VE"},
        "interface": {"type": "string", "description": "J-Link interface; default SWD"},
        "speed": {"description": "J-Link speed kHz or adaptive keyword; default 4000"},
        "timeout_s": {"type": "number"},
    }
    return [
        {
            "name": "build",
            "description": "Run cmake --build --preset <preset>.",
            "inputSchema": obj({"preset": preset_prop, "timeout_s": {"type": "number"}}),
        },
        {
            "name": "artifact_info",
            "description": "Return expected ELF/HEX/BIN paths and existence for a preset.",
            "inputSchema": obj({"preset": preset_prop}),
        },
        {
            "name": "symbol_addr",
            "description": "Resolve a symbol address from the current ELF with arm-none-eabi-nm.",
            "inputSchema": obj({"preset": preset_prop, "symbol": {"type": "string"}}, ["symbol"]),
        },
        {
            "name": "read_symbol_openocd",
            "description": "Resolve symbol from current ELF, then read it with OpenOCD mdw.",
            "inputSchema": obj({
                "preset": preset_prop,
                "symbol": {"type": "string"},
                "byte_count": {"type": "integer"},
                "halt": {"type": "boolean", "description": "Halt target before reading; default true"},
                "resume": {"type": "boolean", "description": "Resume target after reading; default false"},
            }, ["symbol"]),
        },
        {
            "name": "write_symbol_openocd",
            "description": "Resolve symbol from current ELF, then write one word with OpenOCD mww.",
            "inputSchema": obj({
                "preset": preset_prop,
                "symbol": {"type": "string"},
                "value": {"description": "Integer or 0x-prefixed string"},
                "allow_large_symbol": {"type": "boolean"},
                "halt": {"type": "boolean", "description": "Halt target before writing; default true"},
                "resume": {"type": "boolean", "description": "Resume target after writing; default false"},
            }, ["symbol", "value"]),
        },
        {
            "name": "mcf_snapshot_openocd",
            "description": "Read the standard MCF8329A debug globals by symbol from the current ELF.",
            "inputSchema": obj({
                "preset": preset_prop,
                "halt": {"type": "boolean", "description": "Halt target before snapshot; default true"},
                "resume": {"type": "boolean", "description": "Resume target after snapshot; default false"},
                "timeout_s": {"type": "number"},
            }),
        },
        {
            "name": "openocd_cmd",
            "description": "Run a bounded OpenOCD command. Use symbol tools instead of raw addresses when possible.",
            "inputSchema": obj({
                "cmd": {"type": "string"},
                "allow_raw": {"type": "boolean"},
                "timeout_s": {"type": "number"},
            }, ["cmd"]),
        },
        {
            "name": "flash_openocd",
            "description": "Flash a preset artifact with OpenOCD program verify.",
            "inputSchema": obj({
                "preset": preset_prop,
                "artifact": {"type": "string", "enum": ["hex", "bin", "elf"]},
                "reset": {"type": "boolean"},
                "timeout_s": {"type": "number"},
            }),
        },
        {
            "name": "jlink_cmd",
            "description": "Run bounded J-Link Commander commands. Use symbol tools instead of raw addresses when possible.",
            "inputSchema": obj({
                "cmd": {"type": "string", "description": "One or more J-Link Commander commands separated by newlines"},
                "commands": {"type": "array", "items": {"type": "string"}},
                "allow_raw": {"type": "boolean"},
                **jlink_props,
            }),
        },
        {
            "name": "flash_jlink",
            "description": "Flash a preset artifact or explicit artifact_path with J-Link Commander loadfile.",
            "inputSchema": obj({
                "preset": preset_prop,
                "artifact": {"type": "string", "enum": ["hex", "bin", "elf"]},
                "artifact_path": {"type": "string"},
                "address": {"description": "Required for .bin unless preset has a known flash base"},
                "base_address": {"description": "Alias for address"},
                "reset": {"type": "boolean"},
                **jlink_props,
            }),
        },
        {
            "name": "read_symbol_jlink",
            "description": "Resolve symbol from current ELF, then read it with J-Link Commander mem8/mem32.",
            "inputSchema": obj({
                "preset": preset_prop,
                "symbol": {"type": "string"},
                "byte_count": {"type": "integer"},
                "halt": {"type": "boolean", "description": "Halt target before reading; default true"},
                "resume": {"type": "boolean", "description": "Resume target after reading; default false"},
                **jlink_props,
            }, ["symbol"]),
        },
        {
            "name": "write_symbol_jlink",
            "description": "Resolve symbol from current ELF, then write one word with J-Link Commander w4.",
            "inputSchema": obj({
                "preset": preset_prop,
                "symbol": {"type": "string"},
                "value": {"description": "Integer or 0x-prefixed string"},
                "allow_large_symbol": {"type": "boolean"},
                "halt": {"type": "boolean", "description": "Halt target before writing; default true"},
                "resume": {"type": "boolean", "description": "Resume target after writing; default false"},
                **jlink_props,
            }, ["symbol", "value"]),
        },
        {
            "name": "serial_list",
            "description": "List likely serial devices on this host.",
            "inputSchema": obj({}),
        },
        {
            "name": "serial_capture",
            "description": "Capture serial bytes for a short duration. Requires pyserial.",
            "inputSchema": obj({
                "port": {"type": "string"},
                "baud": {"type": "integer"},
                "duration_s": {"type": "number"},
            }, ["port"]),
        },
    ]


class McpStdio:
    def __init__(self, server: Stm32DebugServer):
        self.server = server

    @staticmethod
    def _send(obj: Dict[str, Any], mode: str) -> None:
        data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        if mode == "header":
            sys.stdout.buffer.write(f"Content-Length: {len(data)}\r\n\r\n".encode("ascii"))
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        else:
            sys.stdout.write(data.decode("utf-8") + "\n")
            sys.stdout.flush()

    @staticmethod
    def _content(payload: Any) -> List[Dict[str, str]]:
        return [{"type": "text", "text": json.dumps(payload, ensure_ascii=False, indent=2)}]

    def handle(self, msg: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        method = msg.get("method")
        msg_id = msg.get("id")
        try:
            if method == "initialize":
                return {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {
                        "protocolVersion": "2024-11-05",
                        "capabilities": {"tools": {}},
                        "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                    },
                }
            if method == "notifications/initialized":
                return None
            if method == "tools/list":
                return {"jsonrpc": "2.0", "id": msg_id, "result": {"tools": tool_schema()}}
            if method == "tools/call":
                params = msg.get("params") or {}
                name = params.get("name")
                args = params.get("arguments") or {}
                result = self.server.call_tool(name, args)
                return {"jsonrpc": "2.0", "id": msg_id, "result": {"content": self._content(result)}}
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32601, "message": f"method not found: {method}"},
            }
        except Exception as exc:
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32000, "message": str(exc)},
            }

    @staticmethod
    def _read_header_message(first_line: bytes) -> Optional[Dict[str, Any]]:
        headers = [first_line]
        while True:
            line = sys.stdin.buffer.readline()
            if line in (b"", b"\r\n", b"\n"):
                break
            headers.append(line)
        length: Optional[int] = None
        for raw in headers:
            text = raw.decode("ascii", errors="replace").strip()
            if text.lower().startswith("content-length:"):
                length = int(text.split(":", 1)[1].strip())
                break
        if length is None:
            raise DebugError("missing Content-Length header")
        body = sys.stdin.buffer.read(length)
        if not body:
            return None
        return json.loads(body.decode("utf-8"))

    def serve(self) -> None:
        while True:
            first = sys.stdin.buffer.readline()
            if not first:
                return
            if not first.strip():
                continue
            mode = "header" if first.lower().startswith(b"content-length:") else "line"
            try:
                if mode == "header":
                    msg = self._read_header_message(first)
                    if msg is None:
                        return
                else:
                    msg = json.loads(first.decode("utf-8"))
            except Exception as exc:
                self._send({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": str(exc)}}, mode)
                continue
            resp = self.handle(msg)
            if resp is not None:
                self._send(resp, mode)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[2]), help="Repo root")
    args = ap.parse_args()
    McpStdio(Stm32DebugServer(Path(args.root))).serve()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
