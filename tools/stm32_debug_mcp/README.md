# STM32 Debug MCP

This MCP server wraps the repetitive upboard debug workflow:

- build CMake presets
- locate the current ELF/HEX/BIN artifacts
- resolve symbols from the current ELF
- read/write symbols through OpenOCD without hand-copying map addresses
- read/write symbols through J-Link Commander without hand-copying map addresses
- capture a standard MCF8329A debug snapshot
- list/capture serial ports
- flash artifacts through OpenOCD `program ... verify`
- flash artifacts through J-Link Commander `loadfile`

The important rule is: agents should call symbol tools instead of using raw RAM
addresses. Every symbol lookup runs against the current ELF for the requested
preset, so BSS layout changes after rebuilds do not poison debug reads.

## Run

From the repo root:

```bash
python3 tools/stm32_debug_mcp/server.py --root /Users/mac/Github/main-board
```

Example client config:

```json
{
  "mcpServers": {
    "stm32-debug": {
      "command": "python3",
      "args": [
        "/Users/mac/Github/main-board/tools/stm32_debug_mcp/server.py",
        "--root",
        "/Users/mac/Github/main-board"
      ]
    }
  }
}
```

## First Tools To Use

- `build({"preset":"app-b"})`
- `artifact_info({"preset":"app-b"})`
- `symbol_addr({"preset":"app-b","symbol":"g_mcf_spin_rc"})`
- `read_symbol_openocd({"preset":"app-b","symbol":"g_mcf_spin_rc"})`
- `read_symbol_jlink({"preset":"app-b","symbol":"g_mcf_spin_rc","device":"STM32F407VE"})`
- `mcf_snapshot_openocd({"preset":"app-b"})`

`read_symbol_openocd` and `mcf_snapshot_openocd` start OpenOCD, halt the target,
read values, then exit. They do not reset the target. Pass `resume=true` when
you want execution to continue after the observation.

J-Link tools use `JLinkExe` and default to `device=STM32F407VE`,
`interface=SWD`, and `speed=4000`. Override per call or with environment
variables:

- `STM32_DEBUG_MCP_JLINK_DEVICE` / `JLINK_DEVICE`
- `STM32_DEBUG_MCP_JLINK_INTERFACE` / `JLINK_INTERFACE`
- `STM32_DEBUG_MCP_JLINK_SPEED` / `JLINK_SPEED`
- `STM32_DEBUG_MCP_JLINK_EXE`

For projects beyond upboard, unknown presets are supported when
`build/<preset>/` contains a discoverable unique `.elf`, `.hex`, or `.bin`.
For `.bin` flashing with J-Link, pass `address`/`base_address` unless the
preset is one of the known upboard slots.

## Safety Notes

- Prefer `read_symbol_openocd` over `openocd_cmd`.
- `openocd_cmd` refuses non-debug commands unless `allow_raw=true`.
- `write_symbol_openocd` writes one 32-bit word to a symbol resolved from the
  current ELF. It refuses large symbols unless `allow_large_symbol=true`.
- `flash_openocd` programs the selected artifact and verifies it; it does not
  mass erase.
- `flash_jlink` runs J-Link `loadfile`; inspect `verified_hint` and the raw
  J-Link output before reporting success.
- Serial capture requires `pyserial` in the Python environment.
