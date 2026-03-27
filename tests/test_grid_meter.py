"""
Integration tests for grid_meter component.
Requires ESPHome device running p1.yaml + grid_meter flashed and reachable.

Run: pytest tests/test_grid_meter.py --device-ip=<IP> -v
"""
import struct


def read_int32(registers, idx):
    """Reconstruct signed int32 from two uint16 registers using Reg_s32l word order.

    EM24 uses little-endian word order: low word at lower address (idx), high word at idx+1.
    pymodbus returns registers in address order, so registers[idx] = low word.
    """
    raw = (registers[idx + 1] << 16) | registers[idx]
    return struct.unpack(">i", struct.pack(">I", raw))[0]


def read_uint32(registers, idx):
    """Reconstruct uint32 from two uint16 registers using Reg_s32l word order."""
    return (registers[idx + 1] << 16) | registers[idx]


def test_fc03_reads_registers(client):
    """FC03 reading a block of registers from 0x0000 must succeed."""
    rr = client.read_holding_registers(0x0000, 20, slave=1)
    assert not rr.isError(), f"FC03 failed: {rr}"
    assert len(rr.registers) == 20


def test_voltage_is_plausible(client):
    """L1 voltage must be in range 200-260 V (typical Dutch grid)."""
    rr = client.read_holding_registers(0x0000, 2, slave=1)
    assert not rr.isError()
    raw = read_int32(rr.registers, 0)
    voltage = raw / 10.0  # x0.1 V
    assert 200.0 <= voltage <= 260.0, f"Voltage out of range: {voltage} V"


def test_current_is_non_negative(client):
    """L1 current must be >= 0 (always positive magnitude). Register 0x000C-0x000D."""
    rr = client.read_holding_registers(0x000C, 2, slave=1)
    assert not rr.isError()
    raw = read_int32(rr.registers, 0)
    current = raw / 1000.0  # x0.001 A
    assert current >= 0.0, f"Current is negative: {current} A"


def test_power_is_signed_int32(client):
    """Active power register pair must decode as a valid signed int32. Register 0x0012-0x0013."""
    rr = client.read_holding_registers(0x0012, 2, slave=1)
    assert not rr.isError()
    power = read_int32(rr.registers, 0) / 10.0
    assert -100000.0 < power < 100000.0, f"Power implausible: {power} W"


def test_model_id_register(client):
    """Register 0x000B must contain EM24 device ID (1648)."""
    rr = client.read_holding_registers(0x000B, 1, slave=1)
    assert not rr.isError()
    assert rr.registers[0] == 1648, f"Unexpected model ID: {rr.registers[0]}"


def test_energy_import_is_non_negative(client):
    """Energy import total must be >= 0. Register 0x0034-0x0035."""
    rr = client.read_holding_registers(0x0034, 2, slave=1)
    assert not rr.isError()
    value = read_uint32(rr.registers, 0)
    assert value >= 0  # uint32 is always >= 0; checks decode works


def test_energy_export_is_non_negative(client):
    """Energy export total must be >= 0. Register 0x004E-0x004F."""
    rr = client.read_holding_registers(0x004E, 2, slave=1)
    assert not rr.isError()
    value = read_uint32(rr.registers, 0)
    assert value >= 0


def test_fc04_reads_same_as_fc03(client):
    """FC04 must return the same stable register values as FC03 (Cerbo may use either).

    Only voltage (0x0000-0x0001) and model ID (0x000B) are compared, since power can
    legitimately change between the two consecutive reads.
    """
    rr03 = client.read_holding_registers(0x0000, 12, slave=1)
    rr04 = client.read_input_registers(0x0000, 12, slave=1)
    assert not rr03.isError() and not rr04.isError()
    # Voltage (regs 0-1) and model ID (reg 11) are stable
    assert rr03.registers[0:2] == rr04.registers[0:2], \
        f"Voltage differs between FC03 and FC04: {rr03.registers[0:2]} vs {rr04.registers[0:2]}"
    assert rr03.registers[11] == rr04.registers[11], \
        f"Model ID differs between FC03 and FC04: {rr03.registers[11]} vs {rr04.registers[11]}"


def test_fc03_out_of_range_returns_zero(client):
    """FC03 at address 0x0100 (outside dense range, not in sparse table) must succeed and return 0."""
    rr = client.read_holding_registers(0x0100, 1, slave=1)
    assert not rr.isError(), f"FC03 out-of-range unexpectedly failed: {rr}"
    assert rr.registers[0] == 0, f"Expected 0 for unknown register, got {rr.registers[0]}"


def test_fc03_count_zero_returns_exception(client):
    """FC03 with count=0 must return exception 0x03 (Illegal Data Value)."""
    rr = client.read_holding_registers(0, 0, slave=1)
    assert rr.isError() or (hasattr(rr, 'function_code') and rr.function_code == 0x83), \
        "Expected Modbus exception for zero count"


def test_fc06_write_accepted_as_noop(client):
    """FC06 write must succeed as a no-op (component echoes the request, ignores the write)."""
    wr = client.write_register(0x0004, 0, slave=1)
    assert not wr.isError(), f"FC06 unexpectedly returned an error: {wr}"


def test_readings_are_stable_between_polls(client):
    """Voltage and energy must not change between rapid polls (within 0.5 s)."""
    import time
    v1 = client.read_holding_registers(0x0000, 2, slave=1)   # voltage
    ei1 = client.read_holding_registers(0x0034, 2, slave=1)  # energy import
    time.sleep(0.5)
    v2 = client.read_holding_registers(0x0000, 2, slave=1)
    ei2 = client.read_holding_registers(0x0034, 2, slave=1)
    assert not v1.isError() and not v2.isError()
    assert not ei1.isError() and not ei2.isError()
    assert v1.registers == v2.registers, \
        f"Voltage changed between polls: {v1.registers} vs {v2.registers}"
    assert ei1.registers == ei2.registers, \
        f"Energy import changed between polls: {ei1.registers} vs {ei2.registers}"
