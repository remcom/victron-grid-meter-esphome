# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

An ESPHome external component (`grid_meter`) that emulates a Carlo Gavazzi EM24 Ethernet energy meter over Modbus TCP (port 502), so a Victron Cerbo GX can treat a Dutch DSMR P1 smart meter connected to an ESP32 as a grid energy meter.

## Running Tests

**Schema tests** (no hardware required):
```bash
pytest tests/test_schema.py -v
```

**Integration tests** (requires flashed device on network):
```bash
pytest tests/test_grid_meter.py --device-ip=<ESP32_IP> -v
```

Install dependencies: `pip install -r requirements.txt`

## Architecture

### Component structure

```
components/grid_meter/
  __init__.py       # ESPHome config schema + codegen (Python)
  grid_meter.h      # GridMeterComponent class declaration
  grid_meter.cpp    # TCP server + Modbus protocol + sensor refresh
tests/
  test_schema.py    # Schema validation without hardware
  test_grid_meter.py # Live Modbus TCP integration tests
```

### How it works

`GridMeterComponent` (a standard ESPHome `Component`) runs a non-blocking Modbus TCP server in `loop()`. On every loop it:
1. Reads live values from eight ESPHome sensor references (populated by the DSMR P1 component)
2. Writes them into `registers_[]` — a dense `uint16_t[80]` array covering EM24 addresses `0x0000–0x004F`
3. Accepts/reads/processes client connections (up to 2 simultaneous)

Sparse EM24 registers outside the dense range (`0x0302`, `0x0304`, `0x1002`, `0xa000`, `0xa100`) are handled in `get_register_()` with hardcoded constant values.

### Key protocol details

- **Why EM24, not ET112:** Victron's `dbus-modbus-client` only recognises EM24 model IDs (1648–1653) over TCP; ET112 IDs only work via RS485.
- **Word order:** All int32 values use `Reg_s32l` (little-endian word order — low word at lower address). The pymodbus client in tests reconstructs them big-endian — see `read_int32()` in `test_grid_meter.py`.
- **FC06/FC16 writes** are accepted as no-ops (required for Cerbo init sequence).
- **hold-on-NaN:** Voltage and current use shadow registers (`voltage_shadow_`, `current_shadow_`) that retain the last valid value when the sensor reports NaN.
- **Power sign:** net power = `power_import - power_export`; positive = importing from grid.
- **Current:** always stored as positive magnitude regardless of power direction.

### ESPHome integration

`__init__.py` declares all eight sensor keys as `cv.Required` and passes them as constructor arguments to `GridMeterComponent` via `cg.new_Pvariable()`. `cv.only_with_esp_idf` is enforced via `cv.All()` in the schema.

### Framework requirement

ESP-IDF only (uses POSIX sockets: `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()`, `fcntl()`). Not compatible with the Arduino framework.

## ESPHome Coding Standards

Follow https://github.com/esphome/esphome/blob/dev/.ai/instructions.md. Key rules:
- Use `this->` prefix on all class member access
- C++17 namespace syntax: `namespace esphome::grid_meter {`
- `static_cast<>` / `reinterpret_cast<>` — no C-style casts
- `static constexpr` for namespace-scope constants (not `static const`)
- Required + invariant fields → constructor parameters, not setters
- `dump_config()` must be implemented

**Schema test caveat:** `pytest tests/test_schema.py` will fail `test_valid_config_accepted` if ESPHome `CORE` is not initialised with an ESP-IDF framework context, because `cv.only_with_esp_idf` is part of the schema.
