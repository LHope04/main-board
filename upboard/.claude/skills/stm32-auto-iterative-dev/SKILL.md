---
name: stm32-auto-iterative-dev
description: Run an automatic iterative STM32 firmware workflow in any CMake-Presets + arm-none-eabi-gcc + ST-Link project: edit code, build with the right preset, flash, verify by memory read or serial, and repeat until a quantified pass condition is met. Use when the user asks to keep debugging until something works rather than stopping after a single edit. Toolchain-agnostic about how many images the project produces (single-image, bootloader+app, OTA A/B) — preset list is discovered at runtime.
---

# Auto Iterative Development

Use this skill when the task requires repeated edit / build / flash / verify loops. Keep iterating until the requested behavior is verified or a concrete external blocker prevents progress.

This skill composes two siblings:
- `stm32-cmake-stlink` — the discover/build/flash/verify workflow it leans on each iteration.
- `stm32-stlink-workflow` — the ST-LINK_CLI / OpenOCD command surface for verification reads.

It adds the **iteration loop**, **hypothesis discipline**, and **pitfalls list** on top.

## Step 0 — Discover project paths (run once at start)

Do NOT hardcode preset names, image filenames, or output subdirs.

```bash
# 1. CMake-Presets root
PRESETS=$(find . -maxdepth 3 -name "CMakePresets.json" 2>/dev/null | head -1)
[ -z "$PRESETS" ] && { echo "No CMakePresets.json — this skill targets CMake-Presets projects."; exit 1; }
ROOT=$(dirname "$PRESETS")

# 2. Non-hidden configurePresets (one per image)
PRESETS_LIST=$(python -c "
import json
d = json.load(open(r'$PRESETS', encoding='utf-8'))
print(' '.join(p['name'] for p in d.get('configurePresets', []) if not p.get('hidden')))
")
echo "Presets: $PRESETS_LIST"

# 3. ST-Link tool
STLINK=$(command -v ST-LINK_CLI.exe 2>/dev/null \
    || find "/c/Program Files (x86)/STMicroelectronics" -name "ST-LINK_CLI.exe" 2>/dev/null | head -1)
echo "ST-Link: ${STLINK:-NOT FOUND}"

# 4. Optional serial monitor (only some projects ship one)
SERIAL_MON=$(find . -name "serial_monitor.py" 2>/dev/null | head -1)
echo "Serial monitor: ${SERIAL_MON:-NOT FOUND — use memory reads only}"

# 5. Python with pyserial (needed only if SERIAL_MON exists)
[ -n "$SERIAL_MON" ] && PYBIN=$(python -c "import serial,sys; print(sys.executable)" 2>/dev/null)
```

Save before continuing:
- `ROOT`, `PRESETS_LIST`, `STLINK`, `SERIAL_MON` (optional), `PYBIN` (optional)

For each preset, the build dir is `build/<preset>/` and the output files land in `build/<preset>/<sub>/<image>.{elf,hex,bin,map}` where `<sub>` and `<image>` are decided by the preset's `add_subdirectory()` / `add_executable()`. Discover with `ls build/<preset>/**/*.{elf,hex,bin,map}` after the first build.

## Default loop

1. **State the hypothesis.** Before touching code, write one sentence: what you expect to be wrong, what fix you'll try, what observation will prove it. No silent edits.
2. **Edit code** for the requested behavior.
3. **Add minimal test points** when needed:
   - counters (uint32 incremented in the loop / ISR you want to verify)
   - status flags written from the path under test
   - `snprintf(buf,...) + uart_send_string(buf)` if serial is needed (do **not** rely on `printf` unless the project explicitly retargets it)
   - any variable readable later via `-r32` against its `.map` address
4. **Pick which preset(s) to rebuild** (see *Affected preset rules* below).
5. **Build incrementally** — ninja figures out which `.o` to redo:
   ```bash
   cmake --build --preset <name>
   # or, if shared code changed:
   for p in $PRESETS_LIST; do cmake --build --preset $p || break; done
   ```
   Build success = exit 0 AND the link step prints a `Memory region Used Size` table. No log file to grep.
6. **Flash** — use the preset's actual output paths (discover with `ls build/<preset>/**/*.hex`):
   ```bash
   "$STLINK" -c SWD UR -P build/<preset>/<sub>/<image>.hex -V after_programming -Rst
   ```
   Add `-Rst` only on the **last** image in a multi-image flash. Multi-image: each `.hex` carries its own LMA, so flashing one image does not touch sectors used by another (assuming non-overlapping linker scripts — verify if unsure).
   Flash success = ST-LINK_CLI prints **both** `Programming Complete` and `Verification...OK`.
7. **Verify — memory reads first.** Always.
   ```bash
   grep -E "\b<symbol>\b" build/<preset>/<sub>/<image>.map     # find address
   "$STLINK" -c SWD HotPlug -r32 0x2000xxxx 1                  # read one word, non-invasive
   ```
   Memory reads beat serial because they require no wiring, no port permission, no tail of stale logs, and the CPU keeps running.
8. **Fall back to serial** only when memory cannot answer (timing, ordering, printf traces). If `SERIAL_MON` is empty, state that serial is unavailable and continue with memory reads.
9. **If verification passes**, stop and deliver the result with the observed value and the threshold it met.
10. **If it fails**, write the new hypothesis (step 1) — do not just re-try the same change with a tweak. Loop.

## Affected preset rules (which images to rebuild)

| Edit location | Rebuild |
|---|---|
| Inside one image's subdir only (e.g. `bootloader/Src/foo.c`) | That preset only |
| Shared code (`Common/`, `Drivers/`, `Core/Src/system_*.c`, top-level `CMakeLists.txt`, toolchain file) | Every preset |
| Linker script `cmake/ld/<X>.ld` | The preset(s) whose `LINK_DEPENDS` references it |
| Preset definition (`CMakePresets.json`) | Reconfigure (`cmake --preset <name>`) then build that preset |

When in doubt, rebuild everything — ninja's incremental build skips untouched `.o` files anyway. Don't manually `rm -rf build/` to "force a clean build" unless a CMake graph corruption is actually proven; you throw away the entire dep graph for nothing.

## Iteration discipline

- **One hypothesis at a time.** If your edit changes both "the timer config" and "the ISR body," and the result is still wrong, you can't tell which was right and which was wrong. Bisect.
- **Counters > breakpoints** for "is this code path running" questions. A counter you read with `-r32` in 2 seconds beats a 30-second GDB attach.
- **Snapshot for multi-variable state.** Reading three `volatile`s with three `-r32` calls means three different points in time — your numbers will not be self-consistent. Make a `struct` snapshot, copy with interrupts disabled, then read the snapshot's address.
- **Quantified pass condition agreed up front.** Before iterating, write down what "it works" looks like as a number range, register value, or hex pattern. "Looks reasonable" is not a pass.
- **Stop early when blocked.** If verification needs hardware you don't have (a working sensor, a calibrated load), say so and stop — don't fake-pass with a heuristic.

## Common failure modes

- **Build fails**:
  - Read the ninja error line (the file path is in the error itself); open that file at the line; fix; rebuild that preset only.
  - For "undefined reference," the symbol is in a `.c` file the preset's CMakeLists doesn't list — add it.
- **Flash fails (`No ST-LINK detected`)** — probe / wiring / Vtarget; no software fix. Stop and surface.
- **Flash succeeds but device misbehaves** — first read the vector table base via `-r32` and confirm reset handler == expected symbol address from the map. If they disagree, you flashed the wrong slot or the linker script's FLASH origin is wrong for this image.
- **Memory read returns `0x00000000` or `0xFFFFFFFF`** — symbol address from a stale map (rebuild without re-flashing), or the variable lives in `.bss` and hasn't been written yet. Reset and re-read.
- **Sensor / peripheral value stuck at zero** — read the raw register first, not the cooked value. If raw moves but cooked doesn't, the math is wrong. If raw is also zero, the peripheral clock or GPIO is wrong.
- **`printf` produces no output** — many projects don't link a `_write` / `fputc` retarget. Use `snprintf` + the project's existing UART send function instead.

## Pitfalls (cross-project, learned the hard way)

| Pitfall | Symptom | Fix |
|---|---|---|
| Vector table mis-set after multi-image flash | App boots wrong handler / Hard Faults on first IRQ | Verify `VECT_TAB_OFFSET` (or `SCB->VTOR`) matches the LMA of the image; re-link with the correct offset |
| Stack top mis-checked by a parent (e.g. bootloader's MSP sanity check) | Bootloader silently refuses to jump; no fault, no UART | Range-check `msp` against actual SRAM bounds (`< 0x20000000` or `> 0x20000000+SIZE`), not a bitmask — GCC's `_estack` lands at the very top |
| `-r32` with `count > 2` returns truncated data | Multi-variable read is incomplete | Read each address individually |
| `volatile` reads not atomic across multiple `-r32` calls | Inconsistent timestamps / counters | Snapshot struct, copy with interrupts off, read the snapshot |
| TIMx dual IRQ vectors (TIM1/TIM8) | Capture or update IRQ never fires | Enable BOTH `TIMx_UP_*_IRQn` and `TIMx_CC_IRQn`; general-purpose timers (TIM2/3/4/5) use a single vector |
| Mixed PWM + Input Capture on same TIM | IC never triggers, or PWM stops after IC config | `HAL_TIM_PWM_Init` first (initializes the time base), then `HAL_TIM_IC_ConfigChannel`; update IRQ is **not** enabled by `HAL_TIM_IC_Start_IT` — call `__HAL_TIM_ENABLE_IT(&htimx, TIM_IT_UPDATE)` separately |
| `& "..."` PowerShell syntax inside bash | `syntax error near unexpected token '&'` | Drop the `&`; quote-only: `"C:/path/tool.exe"` |
| Reading stale `serial_log.txt` | "Pass" while board still runs old firmware | Check mtime (`stat ... | grep Modify`); restart the monitor; better — switch to memory reads |
| Sensor sign / offset inversion | Cooked value pinned at 0 while raw moves | Print raw; verify whether it's `(raw - offset)` or `(offset - raw)` for this sensor |
| Mass-erase "to clean up" wipes a sibling app slot in OTA projects | Bootloader jumps into 0xFFFFFFFF and Hard Faults | Never mass-erase a multi-image project unless you're prepared to re-flash everything in order |

## Acceptance criteria

Verification is complete only when a **quantified** pass condition is met. Agree on the range before starting:
- Input capture: expected frequency range (e.g. "30–200 Hz at 50% PWM")
- ADC: expected raw range at a known input
- GPIO: expected ODR pattern as a hex mask
- PWM duty: expected `CCR/ARR` ratio

Measured value in range → pass. Out of range → diagnose; do not change code on a hunch.

## Execution rules

- Discover preset names, image names, and output dirs at runtime — never hardcode.
- Use bash syntax (forward slashes, `"$VAR"`, no PowerShell `& "..."`).
- Verify with memory reads by default; serial only when memory cannot answer.
- Always use `HotPlug` for reads on a running target.
- Confirm GPIO / interface mapping with the user before writing peripheral code — partial pin lists cause mid-development rework.
- Do not stop after a single failed attempt if another concrete iteration is available.
- Do not report success without a quantified observation tied to the agreed pass condition.
- If blocked by hardware, missing tools, or unobservable signals, state the blocker precisely and stop — don't iterate speculatively.

## Trigger phrases

This skill is a good match when the user says things like:
- "auto iterative development"
- "write the code and debug it yourself"
- "keep iterating until it works"
- "continue debugging until success"
- "试一下，不行就改" / "自己迭代直到通过"
