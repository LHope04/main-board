---
name: stm32-auto-iterative-dev
description: "Run an automatic iterative STM32 firmware workflow in the current repository: modify code, compile with Keil, flash with ST-Link, verify through serial logs or memory reads, and repeat until the requested behavior passes. Use when the user asks Codex to keep debugging and iterating instead of stopping after a single code change."
---

# Auto Iterative Development

Use this skill when the task requires repeated code-edit, build, flash, and verification loops. Keep iterating until the requested behavior is verified or a concrete external blocker prevents progress.

## Step 0 — Discover Project Paths (run once at start)

Do NOT assume fixed paths. Discover them first:

```bash11
# Find Keil project file
find . -name "*.uvprojx" 2>/dev/null

# Derive hex and build log paths from the project file
# Example: if project is lcd/MDK-ARM/lcd.uvprojx
#   hex      -> lcd/MDK-ARM/lcd/lcd.hex
#   build log-> lcd/MDK-ARM/lcd/lcd.build_log.htm
```

Find the serial monitor script:
```bash
find . -name "serial_monitor.py" 2>/dev/null
```

Find the Python binary that has pyserial:
```bash
python -c "import serial; print(serial.__file__)" 2>/dev/null || \
  "C:/Python314/python.exe" -c "import serial; print(serial.__file__)" 2>/dev/null
```

Save these four paths before continuing:
- `PROJ` — path to `.uvprojx`
- `HEX`  — path to `.hex`
- `LOG`  — path to `.build_log.htm`
- `PYBIN` — python binary with pyserial

## Default Loop

1. Write or modify code for the requested behavior.
2. Add minimal test points when needed:
   - counters
   - markers
   - `snprintf` + `uart_send_string` prints (**not** `printf` — see Pitfalls)
   - variables readable with ST-Link
3. Compile (bash syntax, not PowerShell):

```bash
"C:/Keil_v5/UV4/UV4.exe" -j0 -b "$PROJ"
```

4. Check build result:

```bash
grep -E "Error\(s\)|Warning\(s\)|Program Size" "$LOG" | tail -4
```

Accept only if output contains `0 Error(s)`.

5. Flash:

```bash
ST-LINK_CLI.exe -c SWD -P "$HEX" -V after_programming -Rst
```

6. Treat flashing as successful only if output contains `Verification...OK` and `Programming Complete`.

7. Verify behavior — use serial log AND/OR memory reads:

**Serial log** (start monitor first, then wait, then read):
```bash
# Start monitor in background
"$PYBIN" tools/serial_monitor.py <COM_PORT> &
sleep 8
cat tools/serial_log.txt
```

**Memory read (non-invasive HotPlug mode)**:
```bash
# Always use HotPlug to avoid halting the CPU
ST-LINK_CLI.exe -c SWD HotPlug -r32 <address> <count>

# Find symbol addresses in the map file
grep "symbolname" path/to/output.map
```

8. If verification passes, stop and deliver the result.
9. If verification fails, analyze the failure, choose the next fix, and return to step 1.

## Core Commands

```bash
# Compile (bash)
"C:/Keil_v5/UV4/UV4.exe" -j0 -b "<path/to/project.uvprojx>"

# Flash
ST-LINK_CLI.exe -c SWD -P "<path/to/output.hex>" -V after_programming -Rst

# Memory read — NON-INVASIVE (use HotPlug, not plain SWD)
ST-LINK_CLI.exe -c SWD HotPlug -r32 <address> <word_count>

# Serial monitor (specify full python path with pyserial)
"C:/Python314/python.exe" tools/serial_monitor.py <COM_PORT>

# Find COM port
python -c "import serial.tools.list_ports; [print(p.device, p.description) for p in serial.tools.list_ports.comports()]"
```

## Iteration Strategy

1. Start with the smallest possible test.
2. Add functionality in narrow increments.
3. Use counters to prove loops and periodic tasks are running.
4. Prefer direct memory reads (HotPlug) when serial output is unavailable or ambiguous.
5. Change one main hypothesis at a time when isolating a fault.
6. Check serial_log.txt modification time before reading — stale logs mislead diagnosis.

## Common Checks

- **Compile failure**:
  - `grep -E "error:|warning:" "$LOG"` to find the actual compiler line
  - Summarize the real error before changing code
- **Flash failure**:
  - If `No ST-LINK detected` → hardware not connected, stop and report
  - If `Can't reset the core` → try HotPlug mode or power-cycle the board
  - Do not claim the board is updated if programming did not complete
- **Serial log is empty or stale**:
  - Check file modification time: `stat tools/serial_log.txt | grep Modify`
  - If stale, start the serial monitor first and wait for fresh data
  - If port open fails with PermissionError → another process holds the port, wait or kill it
- **`printf` produces no output**:
  - Keil projects may not enable MicroLib (`useUlib=0` in `.uvprojx`)
  - Without MicroLib, `fputc` retargeting is inactive; `printf` goes to semihosting (no output)
  - Fix: use `snprintf(buf, sizeof(buf), ...) + uart_send_string(buf)` instead
- **Weight/sensor reads stuck at zero**:
  - Check sensor polarity: applying load may decrease raw ADC value (not increase it)
  - Output raw value alongside processed value to diagnose
  - If `raw` changes but weight stays 0, check sign convention and deadband config
- **Peripheral bring-up issues**:
  - Verify GPIO clock is enabled (`__HAL_RCC_GPIOx_CLK_ENABLE()`)
  - Verify pin mode, pull, initial output level
  - Verify SCL/SDA or CLK/DATA wiring assumptions match code

## Pitfalls (learned from this project)

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| `& "..."` PowerShell syntax in bash | `syntax error near unexpected token '&'` | Remove `&`, use `"C:/path/tool.exe"` directly |
| `printf` without MicroLib | No serial output at all | Use `snprintf + uart_send_string` |
| `r32` without HotPlug | `Can't reset the core` | Add `HotPlug` flag |
| Reading stale serial_log.txt | Appears to pass when board runs old firmware | Check file mtime; restart monitor |
| Wrong Python for serial monitor | `ModuleNotFoundError: No module named 'serial'` | Use `python -c "import serial"` to verify; fall back to full path |
| Sensor sign inversion | Processed value always 0 despite raw changing | Output raw; check if `offset - raw` needed instead of `raw - offset` |

## Key Files (project-relative, discover at start)

- Project:   `<discovered>/*.uvprojx`
- Hex:       `<discovered>/<target>/<target>.hex`
- Build log: `<discovered>/<target>/<target>.build_log.htm`
- Map file:  `<discovered>/<target>/<target>.map`  ← for symbol addresses
- Serial log: `tools/serial_log.txt`
- Serial monitor: `tools/serial_monitor.py`

## Execution Rules

- Discover project paths at the start; never assume `upboard` or any fixed name.
- Use bash syntax for all shell commands (not PowerShell).
- Always use `HotPlug` when reading memory from a running target.
- Always start the serial monitor before reading the log file.
- Do not stop after a single failed attempt if another concrete iteration is available.
- Do not report success without a verification step (serial log or memory read).
- If blocked by hardware, missing tools, or unavailable verification signals, state the blocker precisely and stop only when further local iteration would be speculative.

## Trigger Phrases

This skill is a good match when the user says things like:

- `auto iterative development`
- `write code and debug it yourself`
- `keep iterating until it works`
- `continue debugging until success`
