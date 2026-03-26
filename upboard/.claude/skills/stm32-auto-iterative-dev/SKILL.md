---
name: stm32-auto-iterative-dev
description: "Run an automatic iterative STM32 firmware workflow in the current repository: modify code, compile with Keil, flash with ST-Link, verify through serial logs or memory reads, and repeat until the requested behavior passes. Use when the user asks Codex to keep debugging and iterating instead of stopping after a single code change."
---

# Auto Iterative Development

Use this skill when the task requires repeated code-edit, build, flash, and verification loops. Keep iterating until the requested behavior is verified or a concrete external blocker prevents progress.

## Step 0 — Discover Project Paths (run once at start)

Do NOT assume fixed paths. Discover them first:

```bash
# 1. Find Keil executable
KEIL=$(find /c/Keil_v5 /c/Keil /c/Keil_MDK -name "UV4.exe" 2>/dev/null | head -1)
echo "Keil: $KEIL"

# 2. Find Keil project file
PROJ=$(find . -name "*.uvprojx" 2>/dev/null | head -1)
echo "Project: $PROJ"

# 3. Derive target name, hex and build log from project path
# Example: foo/MDK-ARM/bar.uvprojx → target=bar
#   hex      -> foo/MDK-ARM/bar/bar.hex
#   build log-> foo/MDK-ARM/bar/bar.build_log.htm
TARGET=$(basename "$PROJ" .uvprojx)
PROJ_DIR=$(dirname "$PROJ")
HEX="$PROJ_DIR/$TARGET/$TARGET.hex"
LOG="$PROJ_DIR/$TARGET/$TARGET.build_log.htm"
MAP="$PROJ_DIR/$TARGET/$TARGET.map"
echo "Hex: $HEX  Log: $LOG"

# 4. Find serial monitor script (OPTIONAL — not all projects have one)
SERIAL_MON=$(find . -name "serial_monitor.py" 2>/dev/null | head -1)
echo "Serial monitor: ${SERIAL_MON:-NOT FOUND — will use memory reads only}"

# 5. Find Python binary with pyserial (only needed if SERIAL_MON exists)
if [ -n "$SERIAL_MON" ]; then
  PYBIN=$(python -c "import serial; import sys; print(sys.executable)" 2>/dev/null)
  echo "Python: $PYBIN"
fi
```

Save these paths before continuing:
- `KEIL`       — path to `UV4.exe`
- `PROJ`       — path to `.uvprojx`
- `HEX`        — path to `.hex`
- `LOG`        — path to `.build_log.htm`
- `MAP`        — path to `.map` (for symbol addresses)
- `SERIAL_MON` — path to serial monitor script (**optional**, empty if not found)
- `PYBIN`      — python binary with pyserial (only needed if `SERIAL_MON` exists)

## Default Loop

1. Write or modify code for the requested behavior.
2. Add minimal test points when needed:
   - counters
   - markers
   - `snprintf` + `uart_send_string` prints (**not** `printf` — see Pitfalls)
   - variables readable with ST-Link
3. Compile (bash syntax, not PowerShell), using `$KEIL` discovered in Step 0:

```bash
"$KEIL" -j0 -b "$PROJ"
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

7. Verify behavior — **always try memory reads first; fall back to serial only if memory reads cannot prove the behavior**.

**Primary: Memory read (non-invasive HotPlug — never halts CPU)**:
```bash
# Look up symbol address in map file
grep "symbol_name" "$MAP"

# Read one word at a time (count > 2 is unreliable on some ST-LINK versions)
ST-LINK_CLI.exe -c SWD HotPlug -r32 <address> 1

# Read multiple variables individually and correlate
ST-LINK_CLI.exe -c SWD HotPlug -r32 <addr1> 1
ST-LINK_CLI.exe -c SWD HotPlug -r32 <addr2> 1
```

Memory reads are preferred because:
- No serial wiring or port access needed
- Non-invasive — CPU keeps running
- Works even when serial output is missing or held by another process
- ST-LINK HotPlug is always available if the probe is connected

**Fallback: Serial log** (only when the behavior cannot be observed via memory — e.g. timing, printf traces):
```bash
# Only if SERIAL_MON was found in Step 0
# Start monitor in background, wait for output, then read
"$PYBIN" "$SERIAL_MON" <COM_PORT> &
sleep 8
SERIAL_LOG=$(dirname "$SERIAL_MON")/serial_log.txt
cat "$SERIAL_LOG"
```

> If the COM port returns `PermissionError`, another process holds it. Use memory reads instead — do not wait or retry the port.

8. If verification passes, stop and deliver the result.
9. If verification fails, analyze the failure, choose the next fix, and return to step 1.

## Core Commands

All paths use variables discovered in Step 0.

```bash
# Compile
"$KEIL" -j0 -b "$PROJ"

# Check build result
grep -E "Error\(s\)|Warning\(s\)|Program Size" "$LOG" | tail -4

# Flash
ST-LINK_CLI.exe -c SWD -P "$HEX" -V after_programming -Rst

# Memory read — PRIMARY verification method (HotPlug = non-invasive)
# Always read one word at a time; count > 2 is unreliable
grep "symbol_name" "$MAP"                              # find address
ST-LINK_CLI.exe -c SWD HotPlug -r32 <address> 1       # read value

# Find COM port (only needed for serial fallback)
python -c "import serial.tools.list_ports; [print(p.device, p.description) for p in serial.tools.list_ports.comports()]"

# Serial monitor (fallback — only if SERIAL_MON found in Step 0)
"$PYBIN" "$SERIAL_MON" <COM_PORT>
```

## Iteration Strategy

1. Start with the smallest possible test.
2. Add functionality in narrow increments.
3. Use counters to prove loops and periodic tasks are running.
4. **Always try memory reads first.** Only reach for serial when memory cannot prove the behavior (e.g. ordering of events, printf traces).
5. Change one main hypothesis at a time when isolating a fault.
6. If using serial: check `$SERIAL_LOG` modification time before reading — stale logs mislead diagnosis.

## Common Checks

- **Compile failure**:
  - `grep -E "error:|warning:" "$LOG"` to find the actual compiler line
  - Summarize the real error before changing code
- **Flash failure**:
  - If `No ST-LINK detected` → hardware not connected, stop and report
  - If `Can't reset the core` → try HotPlug mode or power-cycle the board
  - Do not claim the board is updated if programming did not complete
- **Serial log is empty or stale** (serial fallback only):
  - Switch to memory reads first — avoids the problem entirely
  - If serial is required: check mtime with `stat "$SERIAL_LOG" | grep Modify`
  - If stale: start the monitor, wait, then re-read
  - If `PermissionError` on port open → another process holds it; use memory reads instead
- **`printf` produces no output**:
  - Keil projects may not enable MicroLib (`useUlib=0` in `.uvprojx`)
  - Without MicroLib, `fputc` retargeting is inactive; `printf` goes to semihosting (no output)
  - Fix: use `snprintf(buf, sizeof(buf), ...) + uart_send_string(buf)` instead
- **Sensor/peripheral value stuck at zero or wrong**:
  - Read the raw register or variable via memory first — skip serial entirely
  - If raw changes but processed value is wrong: check sign convention, scale factor, offset
  - If raw is always zero: check GPIO clock, pin mode, peripheral clock enable
- **Peripheral bring-up issues**:
  - Verify GPIO clock is enabled (`__HAL_RCC_GPIOx_CLK_ENABLE()`)
  - Verify pin mode, pull, alternate function, initial output level
  - Verify SCL/SDA or CLK/DATA wiring assumptions match code
  - Read peripheral status/control registers via HotPlug to confirm configuration took effect

## Pitfalls (learned from this project)

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| `& "..."` PowerShell syntax in bash | `syntax error near unexpected token '&'` | Remove `&`, use `"C:/path/tool.exe"` directly |
| `printf` without MicroLib | No serial output at all | Use `snprintf + uart_send_string` |
| `r32` without HotPlug | `Can't reset the core` | Add `HotPlug` flag |
| Reading stale serial_log.txt | Appears to pass when board runs old firmware | Check file mtime; restart monitor |
| Wrong Python for serial monitor | `ModuleNotFoundError: No module named 'serial'` | Use `python -c "import serial"` to verify; fall back to full path |
| Sensor sign inversion | Processed value always 0 despite raw changing | Output raw; check if `offset - raw` needed instead of `raw - offset` |
| TIM8 dual IRQ vectors | Capture or overflow interrupt never fires | TIM8 splits into two vectors: enable both `TIM8_UP_TIM13_IRQn` (overflow) and `TIM8_CC_IRQn` (capture); general timers (TIM3 etc.) use a single `TIMx_IRQn` |
| Mixed PWM + IC on same TIM | IC never triggers, or PWM stops after IC config | Call `HAL_TIM_PWM_Init` first (initialises base), then `HAL_TIM_IC_ConfigChannel` for IC channels; overflow interrupt is NOT enabled by `HAL_TIM_IC_Start_IT` — call `__HAL_TIM_ENABLE_IT(&htimx, TIM_IT_UPDATE)` separately |
| `-r32 count > 2` returns fewer words than requested | Multi-variable diagnosis yields incomplete data | Read each address individually; do not rely on count > 2 returning all words |
| volatile variables read at different times | val1/val2/overflow from different interrupt moments; calculated frequency is nonsense | Add a snapshot struct; copy all fields with interrupts disabled, then read the snapshot address |

## Key Files (all discovered in Step 0, never hardcoded)

| Variable | Derived from | Purpose |
|----------|-------------|---------|
| `$KEIL` | found under `/c/Keil*` | Build tool |
| `$PROJ` | `find . -name "*.uvprojx"` | Keil project |
| `$HEX` | `$PROJ_DIR/$TARGET/$TARGET.hex` | Flash image |
| `$LOG` | `$PROJ_DIR/$TARGET/$TARGET.build_log.htm` | Build result |
| `$MAP` | `$PROJ_DIR/$TARGET/$TARGET.map` | Symbol → address lookup |
| `$SERIAL_MON` | `find . -name "serial_monitor.py"` | Serial fallback (**optional**) |
| `$SERIAL_LOG` | `$(dirname $SERIAL_MON)/serial_log.txt` | Serial output (**optional**) |
| `$PYBIN` | `python -c "import sys; print(sys.executable)"` | Python for serial only |

## Acceptance Criteria

Verification is only complete when a **quantified** pass condition is met. Before starting a feature, agree on the expected range, not just "looks reasonable":

- Input capture: state the expected frequency range (e.g. "30~200 Hz at 50% PWM")
- ADC: state the expected raw value range at a known temperature/load
- GPIO: state the expected ODR bit pattern as a hex mask
- PWM duty: state the expected CCR value and ARR

If the measured value falls within the agreed range → pass. If not → diagnose before changing code.

## Execution Rules

- Discover all project paths at the start using Step 0; never hardcode `upboard`, `C:/Keil_v5`, or `tools/`.
- Use bash syntax for all shell commands (not PowerShell).
- Always use `HotPlug` when reading memory from a running target.
- **Verify with memory reads by default.** Only use serial when memory cannot answer the question.
- If serial is needed and `$SERIAL_MON` was not found in Step 0, state that serial is unavailable and continue with memory reads only.
- Do not stop after a single failed attempt if another concrete iteration is available.
- Do not report success without a quantified verification step (memory value in expected range, or serial output matching expected pattern).
- Confirm the complete GPIO/interface mapping with the user before writing any peripheral code — partial pin lists cause mid-development rework.
- If blocked by hardware, missing tools, or unavailable verification signals, state the blocker precisely and stop only when further local iteration would be speculative.

## Trigger Phrases

This skill is a good match when the user says things like:

- `auto iterative development`
- `write code and debug it yourself`
- `keep iterating until it works`
- `continue debugging until success`
