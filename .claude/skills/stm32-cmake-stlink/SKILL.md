---
name: stm32-cmake-stlink
description: Build, flash, and verify any STM32 firmware project that uses CMake + arm-none-eabi-gcc + ST-LINK_CLI. Handles both single-image and multi-image (bootloader / OTA A-B / multi-target) layouts by discovering presets at runtime. Use when a `CMakePresets.json` defines one or more `arm-none-eabi-gcc` configurePresets and the user wants to compile, flash, or check target memory.
---

# STM32 CMake + ST-Link Workflow (generic)

This skill makes no assumptions about how many images the project produces, what they are called, or whether they implement OTA. It discovers everything from `CMakePresets.json` and per-preset CMake files at runtime.

## Step 0 — Discover project structure

```bash
# 1. Repo root with CMake presets
PRESETS=$(find . -maxdepth 3 -name "CMakePresets.json" 2>/dev/null | head -1)
[ -z "$PRESETS" ] && { echo "No CMakePresets.json found — this skill needs CMake-Presets-based builds."; exit 1; }
ROOT=$(dirname "$PRESETS")

# 2. Enumerate non-hidden configurePresets
python -c "
import json
d = json.load(open(r'$PRESETS', encoding='utf-8'))
for p in d.get('configurePresets', []):
    if not p.get('hidden'):
        print(p['name'])
"
# Each printed line is a preset that produces one image.
# Single-image project => one line. Multi-image (e.g. bootloader + OTA A/B) => several lines.

# 3. Discover each preset's binary outputs (after first build)
ls $ROOT/build/<preset>/**/*.{elf,hex,bin,map} 2>/dev/null
# CMake's binaryDir is `${sourceDir}/build/${presetName}` by convention; the sub-folder
# inside it depends on add_subdirectory() in CMakeLists.txt.

# 4. ST-LINK_CLI / OpenOCD / arm-none-eabi-gcc on PATH
command -v cmake ninja arm-none-eabi-gcc openocd >/dev/null \
    || echo "Some tools missing from PATH — see stm32-stlink-workflow for install paths."
STLINK=$(command -v ST-LINK_CLI.exe 2>/dev/null || find "/c/Program Files (x86)/STMicroelectronics" -name "ST-LINK_CLI.exe" 2>/dev/null | head -1)
```

Save before continuing:
- `ROOT` — repo root (one above `CMakePresets.json`)
- `PRESETS_LIST` — non-hidden preset names
- `STLINK` — full path to `ST-LINK_CLI.exe` if used (Windows)

## Step 1 — Decide which preset(s) to rebuild

For each preset, look at its `CMakeLists.txt` (top-level + the `add_subdirectory(...)` it pulls in based on the preset's `UPBOARD_TARGET` / equivalent cache variable). The set of source files referenced determines whether the user's edit affects that preset.

Cheap rule of thumb when in doubt:
- If the edit lives **inside one preset's subdir only** (e.g. `bootloader/Src/foo.c`) → rebuild just that preset.
- If the edit is in a **shared dir** (`Common/`, `Drivers/`, `Core/Src/system_stm32f4xx.c`, top-level `CMakeLists.txt`, the toolchain file) → rebuild every preset.
- A change to `cmake/ld/<X>.ld` only affects the preset(s) that reference that script in `LINK_DEPENDS`.

For projects where the dependency map is unclear, rebuild every preset — ninja's incremental will skip untouched .o files anyway.

## Step 2 — Build (incremental)

```bash
# One preset:
cmake --build --preset <name>

# All non-hidden presets:
for p in $PRESETS_LIST; do cmake --build --preset $p || break; done
```

CMake auto-reconfigures when `CMakeLists.txt`, presets, or LINK_DEPENDS files change. A first-time configure is `cmake --preset <name>` — required only when the build dir doesn't yet exist.

Build success = `cmake --build` exits 0 AND the link step prints a `Memory region Used Size` table. No build-log file to scrape.

On failure: read the ninja error line (the file path is in the error itself), open that file at the line, fix it, re-run `cmake --build` for that preset only.

## Step 3 — Locate artifacts

For preset `<P>`, products land in `build/<P>/<some-subdir>/` where the subdir comes from whichever `add_subdirectory()` call this preset enables. Discover:

```bash
ls build/<P>/**/*.elf build/<P>/**/*.hex build/<P>/**/*.bin build/<P>/**/*.map 2>/dev/null
```

Common files per image:
- `<image>.elf` — for OpenOCD/GDB
- `<image>.hex` — Intel HEX, what ST-LINK_CLI flashes by default
- `<image>.bin` — raw binary, for OTA payload or CRC
- `<image>.map` — symbol/section map; `grep "<symbol>" <image>.map` to find an address

Where `<image>` is flashed to is decided by the preset's linker script (look for `MEMORY { FLASH (rx) : ORIGIN = 0x... }` in `cmake/ld/<X>.ld`). Don't guess — read it.

## Step 4 — Flash

```bash
# Single image whose .hex carries its own load address:
"$STLINK" -c SWD UR -P build/<P>/<sub>/<image>.hex -V after_programming -Rst

# Raw .bin to a specific address (e.g. boot params, OTA payloads):
"$STLINK" -c SWD UR -P build/<P>/<sub>/<image>.bin 0x080xxxxx -V after_programming
```

Multi-image flashing rule: each image's `.hex` carries its own LMA, so flashing one image **does not touch** sectors used by another (assuming non-overlapping linker scripts — verify if unsure). Don't mass-erase between images unless you actually need a clean state.

For first-time bring-up of a multi-image project: `"$STLINK" -c SWD UR -ME` (mass erase), then flash images in order (typically: bootloader first, then any params/config blocks, then the active app image with `-Rst` last).

Flash success criterion: ST-LINK_CLI prints both `Programming Complete` and `Verification...OK`. Anything else → stop and surface the exact line.

## Step 5 — Verify

### Memory read (HotPlug, doesn't halt CPU)

```bash
# Find symbol address
grep -E "\b<symbol>\b" build/<P>/<sub>/<image>.map

# Read one word (count > 2 unreliable on some ST-LINK firmwares)
"$STLINK" -c SWD HotPlug -r32 0x20000xxx 1
```

### OpenOCD + GDB (when stepping needed)

```bash
# Server (Terminal 1)
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg

# Client (Terminal 2)
arm-none-eabi-gdb build/<P>/<sub>/<image>.elf \
    -ex "target extended-remote localhost:3333" \
    -ex "monitor reset halt" \
    -ex "load"
```

Or in VSCode: F5 → pick a Cortex-Debug launch config from `.vscode/launch.json`.

### Serial (fallback only)

```bash
SERIAL_MON=$(find . -name "serial_monitor.py" 2>/dev/null | head -1)
[ -n "$SERIAL_MON" ] && python "$SERIAL_MON" -p COM<N>
```

Use only when memory reads can't observe the behavior (timing, printf traces, protocol streams).

## Execution Rules

- Never hardcode preset names, image names, or `build/<preset>/` subdirs in Step 0. Discover them.
- For multi-image projects, build only the affected preset(s); rebuild all only when the edit hits shared code.
- A flash is successful only when ST-LINK_CLI prints `Verification...OK`.
- A behavior is verified only when an explicit memory value, GDB observation, or serial pattern matches expectation.
- If `ST-LINK_CLI` reports `No ST-LINK detected`, the probe is unavailable; stop and report.
- Don't mass-erase to "clean up" — it deletes the bootloader / params / sibling app slots in OTA-style projects.
- Don't blanket reconfigure (`rm -rf build && cmake --preset ...`) when an incremental build would do; that throws away ninja's dep graph for no reason.

## Response Pattern

1. Which preset(s) selected for build, why
2. Build outcome (memory usage line is enough)
3. What got flashed, to where, verify outcome
4. Verification observation (memory value, GDB stop, serial line)
5. Next concrete step if anything failed
