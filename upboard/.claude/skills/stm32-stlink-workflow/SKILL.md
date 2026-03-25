---
name: stm32-stlink-workflow
description: "Build, flash, and debug the STM32F407 firmware project in the current repository by using Keil MDK and ST-LINK_CLI with repository-relative paths. Use when Codex needs to compile `MDK-ARM\\upboard.uvprojx`, inspect `MDK-ARM\\upboard\\upboard.build_log.htm`, program `MDK-ARM\\upboard\\upboard.hex`, or read target memory over SWD."
---

# STM32 ST-Link Development Workflow

Run commands from the repository root and use the exact commands below unless the user explicitly overrides a path or target.

## Environment Setup

### Keil Installation

- Keil executable: `C:\Keil_v5\UV4\UV4.exe`
- Project file: `MDK-ARM\upboard.uvprojx`

### Build Script

```batch
@echo off
"C:\Keil_v5\UV4\UV4.exe" -j0 -b "MDK-ARM\upboard.uvprojx"
```

## Workflow Commands

### 1. Build

Run:

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -j0 -b "MDK-ARM\upboard.uvprojx"
```

Then inspect:

- `MDK-ARM\upboard\upboard.build_log.htm`

Treat the build as successful only if the log confirms `0 Error(s)` or another explicit success summary.

### 2. Flash with ST-Link

```powershell
ST-LINK_CLI.exe -c SWD -P "MDK-ARM\upboard\upboard.hex" -V after_programming -Rst
```

Do not report flash success unless `ST-LINK_CLI.exe` detects the probe and completes programming plus verification.

### 3. Read Memory (Debug)

```powershell
ST-LINK_CLI.exe -c SWD -r32 <address> <count>
```

Use this only when the user provides verification addresses or when a concrete debug check requires memory inspection.

### 4. Full Workflow

Run build first, then flash:

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -j0 -b "MDK-ARM\upboard.uvprojx"
ST-LINK_CLI.exe -c SWD -P "MDK-ARM\upboard\upboard.hex" -V after_programming -Rst
```

Stop after the build step if the build log shows errors.

## Hardware Connection

- Probe: ST-Link over USB
- Debug interface: SWD
- Target MCU: STM32F407

## Execution Rules

- Run commands from the repository root unless the user requests another working directory.
- Prefer repository-relative paths for project artifacts so the workflow stays portable within the repo.
- If `ST-LINK_CLI.exe` reports `No ST-LINK detected!`, state that the board or probe is unavailable and stop before claiming any flash or memory-read result.
- Do not substitute UART logs for verification unless the user explicitly asks for serial validation.

## Key Advantages

- No serial console is required for the default workflow.
- Direct SWD memory reads are available through ST-Link.
- Build, flash, and inspection can all run from the command line.
