# ESPHome Victron Grid Meter — DSMR P1 to Cerbo GX via Modbus TCP

**Turn a Dutch DSMR P1 smart meter into a Victron grid energy meter — no extra hardware required.**

An ESPHome external component that emulates a Carlo Gavazzi EM24 Ethernet energy meter over Modbus TCP (port 502). It bridges Dutch P1 smart meter data (read by an ESP32) to a Victron Cerbo GX, which treats the ESP32 as a real grid energy meter — enabling accurate solar self-consumption tracking, grid feed-in monitoring, and ESS (Energy Storage System) control.

**Keywords:** ESPHome Victron grid meter, DSMR P1 Cerbo GX, ESP32 Modbus TCP grid meter, Carlo Gavazzi EM24 emulator, Victron ESS P1 meter, victron-grid-meter-esphome

## How it works

The component runs a Modbus TCP server directly on the ESP32. Whenever the ESPHome DSMR P1 component pushes new sensor values, they are written into a register array matching the EM24 register map. The Victron Cerbo GX polls this server over TCP and treats the ESP32 as an AC grid energy meter (single-phase or three-phase) — no RS485 adapter, no USB dongle, no extra devices.

If the power sensors stop delivering valid readings (e.g. the P1 cable is unplugged), the component suspends the Modbus server after `data_timeout` (default 30 s) so the Cerbo marks the meter offline instead of acting on frozen values. Service resumes automatically when data returns.

### Why EM24, not ET112?

The Victron Cerbo GX uses `dbus-modbus-client` for TCP energy meters. That driver's `carlo_gavazzi.py` only recognises Carlo Gavazzi EM24 Ethernet model IDs (1648–1653) over TCP — ET112 model IDs (102–121) are only supported via RS485 serial through a separate daemon (`dbus-cgwacs`). This component emulates an EM24 so the Cerbo's TCP driver identifies and polls it correctly.

## Register map (Carlo Gavazzi EM24)

All multi-register values use **little-endian word order** (low word at lower address, `Reg_s32l`).
Per-phase registers step by 2 per phase: L1 at the base address, L2 at base+2, L3 at base+4.

| Address       | Field               | Type   | Scale        | Notes                          |
|---------------|---------------------|--------|--------------|--------------------------------|
| 0x0000–0x0005 | Voltage L1/L2/L3    | int32  | ÷10 V        | hold-on-NaN                    |
| 0x000B        | Model ID            | uint16 | —            | 1648 (EM24DINAV23XE1X)         |
| 0x000C–0x0011 | Current L1/L2/L3    | int32  | ÷1000 A      | positive magnitude; hold-on-NaN |
| 0x0012–0x0017 | Active power L1/L2/L3 | int32 | ÷10 W       | positive = import              |
| 0x0028–0x0029 | Total active power  | int32  | ÷10 W        | sum of configured phases       |
| 0x0033        | Frequency           | uint16 | ÷10 Hz       | hardcoded 50.0 Hz              |
| 0x0034–0x0035 | Energy import total | int32  | ÷10 kWh      | T1+T2                          |
| 0x0040–0x0041 | L1 energy import    | int32  | ÷10 kWh      | = total (DSMR has no per-phase energy) |
| 0x0046–0x0047 | L1 energy export    | int32  | ÷10 kWh      | = total (DSMR has no per-phase energy) |
| 0x004E–0x004F | Energy export total | int32  | ÷10 kWh      | T1+T2                          |
| 0x0302        | HW version          | uint16 | —            | 0x0100 (1.0.0)                 |
| 0x0304        | FW version          | uint16 | —            | 0x0100 (1.0.0)                 |
| 0x1002        | Phase config        | uint16 | —            | 3 = 1P, 0 = 3P.n (auto-selected) |
| 0x5000–0x5006 | Serial number       | text   | —            | from `serial_number:` option, else zeros |
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
    refresh: 1h
    components:
      - grid_meter

sensor:
  - platform: dsmr
    energy_delivered_tariff1:
      name: "Energy Consumed Tariff 1"
      state_class: total_increasing
      id: energy_delivered_tariff1
    energy_delivered_tariff2:
      name: "Energy Consumed Tariff 2"
      state_class: total_increasing
      id: energy_delivered_tariff2
    energy_returned_tariff1:
      name: "Energy Produced Tariff 1"
      state_class: total_increasing
      id: energy_returned_tariff1
    energy_returned_tariff2:
      name: "Energy Produced Tariff 2"
      state_class: total_increasing
      id: energy_returned_tariff2
    power_delivered:
      name: "Power Consumed"
      id: power_delivered
      unit_of_measurement: "W"
      state_class: "measurement"
      accuracy_decimals: 0
      filters:
        - multiply: 1000
    power_returned:
      name: "Power Produced"
      id: power_returned
      unit_of_measurement: "W"
      state_class: "measurement"
      accuracy_decimals: 0
      filters:
        - multiply: 1000
    voltage_l1:
      name: "Voltage Phase 1"
      id: voltage_l1
    current_l1:
      name: "Current Phase 1"
      id: current_l1

grid_meter:
  power_import: power_delivered
  power_export: power_returned
  voltage: voltage_l1
  current: current_l1
  energy_import_t1: energy_delivered_tariff1
  energy_import_t2: energy_delivered_tariff2
  energy_export_t1: energy_returned_tariff1
  energy_export_t2: energy_returned_tariff2
```

All eight sensor keys are required. The sensor IDs must match `id:` fields on sensors already defined in your ESPHome config.

### Optional configuration

| Key | Default | Description |
|-----|---------|-------------|
| `port` | `502` | Modbus TCP listening port. The Cerbo GX expects 502, so only change this for testing. |
| `serial_number` | *(zeros)* | Up to 14 ASCII characters, reported in the EM24 serial registers. Set a unique value per device — VRM uses it to identify the meter (recommended when running more than one). |
| `data_timeout` | `30s` | Suspend the Modbus server when no valid power reading arrives for this long, so the Cerbo marks the meter offline instead of trusting stale data. `0s` disables the watchdog. |

### Three-phase mode

Add per-phase sensors for L2 and/or L3 to emulate a three-phase EM24 (`PhaseConfig` = 3P.n). Each phase group is all-or-nothing: if you set one of the four keys for a phase, you must set all four.

```yaml
grid_meter:
  # ... the eight required keys above (they describe L1) ...
  voltage_l2: voltage_l2
  current_l2: current_l2
  power_import_l2: power_delivered_l2
  power_export_l2: power_returned_l2
  voltage_l3: voltage_l3
  current_l3: current_l3
  power_import_l3: power_delivered_l3
  power_export_l3: power_returned_l3
```

With DSMR, use the per-phase `power_delivered_l1/l2/l3` and `power_returned_l1/l2/l3` sensors (multiplied by 1000 to get W, like the totals). Total power (register 0x0028) is the sum of all configured phases. DSMR provides only total energy counters, so energy remains attributed to L1 in VRM's per-phase view; the totals are always correct.

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
