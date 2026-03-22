# victron-grid-meter-esphome

An ESPHome external component that exposes Dutch DSMR P1 smart meter data as a Carlo Gavazzi EM24-compatible Modbus TCP server on port 502, so a Victron Cerbo GX can read it as a single-phase AC grid meter.

## How it works

The component runs a Modbus TCP server directly on the ESP32. On each loop iteration it reads live sensor values from the ESPHome sensor references (populated by the DSMR P1 component) and writes them into a register array matching the EM24 register map. The Victron Cerbo GX polls this server and treats the device as a grid energy meter.

### Why EM24, not ET112?

The Victron Cerbo GX uses `dbus-modbus-client` for TCP energy meters. That driver's `carlo_gavazzi.py` only recognises Carlo Gavazzi EM24 Ethernet model IDs (1648–1653) over TCP — ET112 model IDs (102–121) are only supported via RS485 serial through a separate daemon (`dbus-cgwacs`). This component emulates an EM24 so the Cerbo's TCP driver identifies and polls it correctly.

## Register map (Carlo Gavazzi EM24 — single phase)

All multi-register values use **little-endian word order** (low word at lower address, `Reg_s32l`).

| Address       | Field               | Type   | Scale        | Notes                          |
|---------------|---------------------|--------|--------------|--------------------------------|
| 0x0000–0x0001 | L1 Voltage          | int32  | ÷10 V        | hold-on-NaN                    |
| 0x000B        | Model ID            | uint16 | —            | 1648 (EM24DINAV23XE1X)         |
| 0x000C–0x000D | L1 Current          | int32  | ÷1000 A      | positive magnitude; hold-on-NaN |
| 0x0012–0x0013 | L1 Active power     | int32  | ÷10 W        | positive = import              |
| 0x0028–0x0029 | Total active power  | int32  | ÷10 W        | same as L1 (single phase)      |
| 0x0033        | Frequency           | uint16 | ÷10 Hz       | hardcoded 50.0 Hz              |
| 0x0034–0x0035 | Energy import total | int32  | ÷10 kWh      | T1+T2                          |
| 0x0040–0x0041 | L1 energy import    | int32  | ÷10 kWh      | same as total (single phase)   |
| 0x004E–0x004F | Energy export total | int32  | ÷10 kWh      | T1+T2                          |
| 0x0302        | HW version          | uint16 | —            | 0x0100 (1.0.0)                 |
| 0x0304        | FW version          | uint16 | —            | 0x0100 (1.0.0)                 |
| 0x1002        | Phase config        | uint16 | —            | 3 = 1P (single phase)          |
| 0xa000        | Application         | uint16 | —            | 7 = H mode (required by Cerbo) |

- Power is signed: positive = importing from grid, negative = exporting
- Current is always positive magnitude (direction inferred from power sign)
- Both FC03 and FC04 are supported
- FC06/FC16 writes are accepted as no-ops (required for Cerbo init sequence)
- Any register not listed above returns 0x0000

## Usage

Add to your ESPHome YAML (e.g. `p1.yaml`):

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/remcom/victron-grid-meter-esphome
      ref: main
    refresh: 1s
    components:
      - grid_meter

grid_meter:
  power_import: power_delivered       # W, sensor ID in your config
  power_export: power_returned        # W
  voltage: voltage_l1                 # V
  current: current_l1                 # A
  energy_import_t1: energy_delivered_tariff1  # kWh
  energy_import_t2: energy_delivered_tariff2  # kWh
  energy_export_t1: energy_returned_tariff1   # kWh
  energy_export_t2: energy_returned_tariff2   # kWh
```

All eight sensor keys are required. The sensor IDs must match `id:` fields on sensors already defined in your ESPHome config.

## Victron Cerbo GX setup

1. Go to **Settings → Energy Meters → Add**
2. Select **Carlo Gavazzi EM24**
3. Enter the ESP32's IP address, port **502**
4. Save — the Cerbo will start polling immediately

## Requirements

- ESP-IDF framework (not Arduino)
- DSMR P1 component providing the sensor data
- `id:` fields on all sensor references used in the `grid_meter:` block

## Running tests

Integration tests require the device to be flashed and reachable on the network.

```bash
pip install -r requirements.txt
pytest tests/test_grid_meter.py --device-ip=<ESP32_IP> -v
```

Schema tests (no hardware required):

```bash
pytest tests/test_schema.py -v
```
