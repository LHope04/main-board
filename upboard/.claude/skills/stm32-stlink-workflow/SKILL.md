---
name: stm32-stlink-workflow
description: Toolchain-agnostic reference for flashing and debugging any STM32 target with an ST-Link probe — covers ST-LINK_CLI and OpenOCD command surface, SWD memory reads, and arm-none-eabi-gdb attach. Use when you already have a `.hex` / `.bin` / `.elf` (from CMake, Make, IAR, or hand-built) and need to program it, read live memory, or run a GDB session, regardless of how the artifact was produced.

  For end-to-end CMake-Presets workflows (configure → build → flash), use `stm32-cmake-stlink` instead — this skill picks up at "I have an artifact, what do I do with it."
---

# STM32 ST-Link Command Reference (toolchain-agnostic)

This skill assumes a built artifact already exists. It does **not** care how the artifact was produced — Keil, CMake, Makefile, IAR, hand-rolled `arm-none-eabi-gcc` invocations all produce the same `.hex` / `.bin` / `.elf` shape.

For build/configure flows, defer to a build-system-specific skill (`stm32-cmake-stlink` for CMake-Presets projects).

## Step 0 — Probe the environment

```bash
# 1. ST-Link CLI (Windows ST-Link Utility install path; varies on Linux/Mac)
STLINK=$(command -v ST-LINK_CLI.exe 2>/dev/null \
    || find "/c/Program Files (x86)/STMicroelectronics" -name "ST-LINK_CLI.exe" 2>/dev/null | head -1 \
    || command -v st-flash 2>/dev/null)
echo "ST-Link tool: ${STLINK:-NOT FOUND}"

# 2. OpenOCD (any install — chocolatey, brew, apt, source)
command -v openocd >/dev/null && OPENOCD=$(command -v openocd) || OPENOCD=""
echo "OpenOCD: ${OPENOCD:-NOT FOUND}"

# 3. arm-none-eabi-gdb (only needed for source-level debug)
command -v arm-none-eabi-gdb >/dev/null && GDB=$(command -v arm-none-eabi-gdb) || GDB=""
echo "GDB: ${GDB:-NOT FOUND}"

# 4. Probe presence (any one of: st-info, ST-LINK_CLI ListAll, openocd dry init)
"$STLINK" -List 2>/dev/null | head -5      # ST-LINK_CLI on Windows
# st-info --probe                          # stlink-tools fallback
```

If `ST-LINK_CLI` reports `No ST-LINK detected` (or `st-info` lists nothing), the probe is unavailable. Stop and surface the blocker — every step below depends on a live SWD link.

## Artifact shape (what each file is for)

| Extension | What it is | When to use |
|---|---|---|
| `.elf` | Linked image with debug symbols + LMA | OpenOCD/GDB load, source-level debug, `arm-none-eabi-objdump` inspection |
| `.hex` | Intel HEX with embedded load addresses | Default flash via `ST-LINK_CLI -P <hex>` (no address arg needed) |
| `.bin` | Raw bytes, no address info | Flash to a specific address: `ST-LINK_CLI -P <bin> 0x080xxxxx` — typical for OTA payloads, params blocks, or images you want to relocate |
| `.map` | Linker symbol/section dump | `grep <symbol> <image>.map` to find the address you want to read with `-r32` |

## ST-LINK_CLI quick reference

All commands assume `STLINK` is set from Step 0. Quote it because the Windows install path contains spaces.

### Flash

```bash
# .hex (load address comes from the file itself)
"$STLINK" -c SWD UR -P <image>.hex -V after_programming

# .bin to a specific address (e.g. params, OTA slot, raw payload)
"$STLINK" -c SWD UR -P <image>.bin 0x080xxxxx -V after_programming

# Add -Rst on the LAST programming step in a multi-image flash to actually run the new code
"$STLINK" -c SWD UR -P <image>.hex -V after_programming -Rst
```

Connect modes:
- `UR` — Connect Under Reset. Default. Works even when the firmware disables SWD pins or runs WFI early.
- `HotPlug` — Attach without resetting. Use for memory reads on a running target; do **not** use for programming.

Verify mode `after_programming` is the standard read-back-and-compare pass. A flash succeeds only if the tool prints **both** `Programming Complete` and `Verification...OK`. Anything else → stop and surface the exact line.

### Erase

```bash
"$STLINK" -c SWD UR -ME                      # mass erase (entire chip)
"$STLINK" -c SWD UR -SE 5                    # sector erase (sector 5)
```

Mass-erase wipes everything — bootloaders, params, sibling OTA slots. Don't use it as a "clean up" step unless you're prepared to re-flash every image from scratch.

### Memory read (non-invasive)

```bash
"$STLINK" -c SWD HotPlug -r32 0x20000000 1   # one word at address
"$STLINK" -c SWD HotPlug -r8  0x20000000 16  # 16 bytes
```

Read **one word at a time** (`count=1`). `count > 2` is unreliable on some ST-LINK firmware revisions and will silently truncate.

To read a named symbol: grep its address out of the linker map first.

```bash
grep -E "\b<symbol>\b" <image>.map           # → 0x2000xxxx
"$STLINK" -c SWD HotPlug -r32 0x2000xxxx 1
```

## OpenOCD quick reference

OpenOCD ships with config snippets for common probes and targets — for ST-Link + STM32 they're auto-located. Replace `target/stm32f4x.cfg` with your family's file (`stm32f1x`, `stm32g0x`, `stm32h7x`, ...).

### As a one-shot command runner

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "init; reset halt; mdw 0x40002850; exit"
```

Useful for scripted reads / writes without spinning up GDB.

### As a GDB server (Terminal 1)

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg
# Listens on localhost:3333 (gdb), :4444 (telnet), :6666 (tcl)
```

### GDB client (Terminal 2)

```bash
arm-none-eabi-gdb <image>.elf \
    -ex "target extended-remote localhost:3333" \
    -ex "monitor reset halt" \
    -ex "load" \
    -ex "monitor reset halt" \
    -ex "break main" \
    -ex "continue"
```

Drop `-ex "load"` when you only want to attach to already-flashed firmware.

To debug across a bootloader → app jump, load symbols for both ELFs in the same session:

```
(gdb) symbol-file bootloader.elf
(gdb) add-symbol-file app.elf
```

In editors, equivalent functionality is exposed by Cortex-Debug (VSCode) — use `symbolFiles: [...]` in `launch.json` to load multiple ELFs.

## Common command recipes

### Verify firmware identity after a flash

```bash
# Build vector table address from the linker map
APP_BASE=$(grep -E "^\s*\.isr_vector\s+0x" <image>.map | awk '{print $2}')
"$STLINK" -c SWD HotPlug -r32 $APP_BASE 1     # initial MSP
"$STLINK" -c SWD HotPlug -r32 $((APP_BASE+4)) 1  # reset handler — should match symbol address
```

### Find a variable's runtime value

```bash
ADDR=$(grep -E "\b<var>\b" <image>.map | awk '{print $1}')   # e.g. 0x20001234
"$STLINK" -c SWD HotPlug -r32 $ADDR 1
```

### Recover from a chip that reset-loops on attach

`UR` (Connect Under Reset) holds NRST low during attach, so it works even when the firmware immediately disables SWD or sleeps. If both `UR` and `HotPlug` fail, the probe / wiring / Vtarget is the issue, not the firmware.

## Execution Rules

- A flash is successful only when ST-LINK_CLI prints `Verification...OK`. Don't skip this line in the report.
- Use `HotPlug` for reads (non-invasive), `UR` for programming (resets + writes safely).
- Read memory **one word at a time** (`-r32 <addr> 1`); larger counts silently truncate on some firmware versions.
- Never mass-erase to "clean up state" — it deletes bootloaders, params, OTA siblings.
- For multi-image projects, flash images in dependency order: bootloader → params/config → app, with `-Rst` only on the very last step.
- If `ST-LINK_CLI` reports `No ST-LINK detected`, stop. The probe is the blocker; there is no software fix.
- Don't claim a behavior is "verified" without a concrete observation: a memory value matching expectation, a GDB stop at the expected line, or a serial pattern matching a documented format.

## When to use which tool

| Task | Tool | Why |
|---|---|---|
| One-shot programming | `ST-LINK_CLI` | Fastest, batch-friendly, scriptable, prints clear pass/fail |
| Reading a few memory words | `ST-LINK_CLI HotPlug -r32` | No GDB session needed, non-invasive |
| Source-level stepping, breakpoints, watch | `openocd` + `arm-none-eabi-gdb` (or VSCode Cortex-Debug) | Only path to true debug |
| Scripted register pokes / sequential memory ops | `openocd -c "init; ...; exit"` | Tcl scripting, transactional |
| CI / unattended programming | `ST-LINK_CLI` exit codes + `grep "Verification...OK"` | Deterministic |
