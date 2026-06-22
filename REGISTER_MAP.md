# Carlo Gavazzi EM24 Modbus Register Map

The ESP32 emulates a **Carlo Gavazzi EM24 DINAV53XE1X** (model ID `1651`) using the
register map defined in
[`dbus-modbus-client/carlo_gavazzi.py`](https://github.com/victronenergy/dbus-modbus-client/blob/master/carlo_gavazzi.py).

## Physical layer

| Parameter | Value |
|-----------|-------|
| Protocol | Modbus TCP |
| Port | 502 |
| Unit ID | 1 (default) |
| Function codes | FC 0x03 (Read Holding Registers), FC 0x04 (Read Input Registers) |

## Identification registers (written at boot)

| Reg (hex) | Value | Description |
|-----------|-------|-------------|
| 0x000B | 1651 | Model ID — EM24DINAV53XE1X (3-phase) |
| 0x0302 | 0x0100 | Hardware version |
| 0x0304 | 0x0100 | Firmware version |
| 0x1002 | 0x0000 | Phase config: 0 = 3P.n (three-phase), 3 = 1P (single-phase) |
| 0x5000–5006 | 0x3030 | Serial number (7 registers, ASCII "0000000") |
| 0xa000 | 7 | Application mode H (required by driver) |
| 0xa100 | 3 | Switch position: 3 = Locked |

The Cerbo GX probes register `0x000B` over TCP to identify the meter.
A value of `1651` causes it to recognize the device as an EM24DINAV53XE1X.

## Register encoding

All multi-word values use **little-endian word order** (s32l): the low 16-bit word is at
the lower register address.

| Type | Size | Description |
|------|------|-------------|
| s32l | 2 regs | Signed 32-bit, little-endian word order (low word first) |
| u16 | 1 reg | Unsigned 16-bit integer |

## Measurement registers

### Per-phase (n = 1, 2, 3)

| Measurement | Base addr | Step | Type | Scale | Unit |
|-------------|-----------|------|------|-------|------|
| Voltage L-N | 0x0000 + 2×(n−1) | 2 | s32l | ÷10 | V |
| Current | 0x000C + 2×(n−1) | 2 | s32l | ÷1000 | A |
| Active power | 0x0012 + 2×(n−1) | 2 | s32l | ÷10 | W |
| Energy forward | 0x0040 + 2×(n−1) | 2 | s32l | ÷10 | kWh |

Expanded:

| Phase | Voltage | Current | Power | Energy Fwd |
|-------|---------|---------|-------|------------|
| L1 | 0x0000–01 | 0x000C–0D | 0x0012–13 | 0x0040–41 |
| L2 | 0x0002–03 | 0x000E–0F | 0x0014–15 | 0x0042–43 |
| L3 | 0x0004–05 | 0x0010–11 | 0x0016–17 | 0x0044–45 |

### Totals

| Reg (hex) | Type | Scale | Unit | Description |
|-----------|------|-------|------|-------------|
| 0x0028–29 | s32l | ÷10 | W | Total active power (auto-summed from phases) |
| 0x0033 | u16 | ÷10 | Hz | Frequency |
| 0x0034–35 | s32l | ÷10 | kWh | Total energy forward (import) |
| 0x004E–4F | s32l | ÷10 | kWh | Total energy reverse (export) |

## Lambda scaling summary

| Sensor (HA unit) | Typed setter | Raw value written |
|------------------|-------------|-------------------|
| Voltage V | `set_voltage(phase, v)` | `(int32_t)(v * 10)` |
| Current A | `set_current(phase, a)` | `(int32_t)(a * 1000)` |
| Power W | `set_power(phase, w)` | `(int32_t)(w * 10)` |
| Frequency Hz | `set_frequency(hz)` | `(uint16_t)(hz * 10)` |
| Energy kWh | `set_energy_import(kwh)` | `(int32_t)(kwh * 10)` |
| Energy kWh | `set_energy_export(kwh)` | `(int32_t)(kwh * 10)` |
| Energy kWh | `set_phase_energy_import(n, kwh)` | `(int32_t)(kwh * 10)` |
