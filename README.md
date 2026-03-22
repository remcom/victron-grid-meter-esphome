# victron-grid-meter-esphome

An ESPHome external component that exposes Dutch DSMR P1 smart meter data as a Carlo Gavazzi ET112-compatible Modbus TCP server on port 502, so a Victron Cerbo GX can read it as a single-phase AC grid meter.

## How it works

The component runs a Modbus TCP server directly on the ESP32. On each loop iteration it reads live sensor values from the ESPHome sensor references (populated by the DSMR P1 component) and writes them into an 18-register array matching the ET112 register map. The Victron Cerbo GX polls this server and treats the device as a grid energy meter.

## Register map (Carlo Gavazzi ET112)

| Address     | Field               | Type   | Scale    |
|-------------|---------------------|--------|----------|
| 0x0000–0x0001 | L1 Voltage        | int32  | ×0.1 V   |
| 0x0002–0x0003 | L1 Current        | int32  | ×0.001 A |
| 0x0004–0x0005 | L1 Active power   | int32  | ×0.1 W   |
| 0x0006–0x000D | Reserved          | —      | 0x0000   |
| 0x000E–0x000F | Energy import total | uint32 | ×0.1 Wh |
| 0x0010–0x0011 | Energy export total | uint32 | ×0.1 Wh |

- Power is signed: positive = importing from grid, negative = exporting
- Current is always a positive magnitude (direction inferred from power sign)
- Both FC03 and FC04 are supported (Cerbo GX may use either)

> **Note:** Register addresses are based on the Carlo Gavazzi EM/ET series Modbus documentation. Verify against the official ET112 manual before deploying.

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
2. Select **Carlo Gavazzi ET112**
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
