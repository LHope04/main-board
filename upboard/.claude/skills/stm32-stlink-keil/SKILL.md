---
name: stm32-stlink-keil
description: Build, flash, and verify STM32 firmware projects that use Keil MDK and ST-LINK_CLI. Use when Codex is working in an STM32 firmware repository with a `.uvprojx` project and needs to compile code, program the board through ST-Link, inspect target memory, or optionally monitor serial output during bring-up and debugging.
---

# STM32 ST-Link Keil

Follow this workflow unless the user asks for a different order.

## Default Workflow

1. Confirm the Keil project and output paths from the repository root.
2. Run the Keil build command from the project root:

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -j0 -b "MDK-ARM\upboard.uvprojx"
```

3. Check the build result before flashing. If the build fails, stop, summarize the compiler errors, and do not flash.
4. Flash the generated hex with ST-Link:

```powershell
ST-LINK_CLI.exe -c SWD -P "MDK-ARM\upboard\upboard.hex" -V after_programming -Rst
```

5. If the user gave verification addresses, read memory through ST-Link and compare the observed values with the expected result:

```powershell
ST-LINK_CLI.exe -c SWD -r32 <address> <count>
```

6. Report build, flash, and verification results clearly. Include the exact command that failed when any step does not succeed.

## Project Paths

Treat these paths as relative to the repository root unless the user overrides them:

- Project file: `MDK-ARM\upboard.uvprojx`
- Hex file: `MDK-ARM\upboard\upboard.hex`
- Build log: `MDK-ARM\upboard\upboard.build_log.htm`

## Execution Rules

- Run commands from the repository root.
- Build before every flash unless the user explicitly asks to flash an existing artifact.
- Do not claim flash success without running the ST-Link command and checking its result.
- Do not claim firmware verification without an explicit memory read or another concrete runtime check.
- If tooling is missing, the board is disconnected, or the command needs elevated permissions, say so and surface the exact blocker.

## Optional Serial Monitor

Use the serial monitor only when runtime logs are needed:

- Script: `tools\serial_monitor.py`
- Log file: `tools\serial_log.txt`
- Example command:

```powershell
python tools\serial_monitor.py -p COM9
```

## Response Pattern

When using this skill, prefer short status updates in this order:

1. Build result
2. Flash result
3. Verification result
4. Runtime serial observations if collected
