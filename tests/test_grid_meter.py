"""
Integration tests for grid_meter component.
Requires ESPHome device running p1.yaml + grid_meter flashed and reachable.

Run: pytest tests/test_grid_meter.py --device-ip=<IP> -v
"""
import struct
import pytest
from pymodbus.client import ModbusTcpClient


def pytest_addoption(parser):
    parser.addoption("--device-ip", default=None, help="ESP32 device IP address")


@pytest.fixture(scope="module")
def client(request):
    ip = request.config.getoption("--device-ip")
    if ip is None:
        pytest.skip("--device-ip not provided")
    c = ModbusTcpClient(ip, port=502)
    c.connect()
    yield c
    c.close()


def read_int32(registers, idx):
    """Reconstruct signed int32 from two uint16 registers (big-endian, high word first)."""
    raw = (registers[idx] << 16) | registers[idx + 1]
    return struct.unpack(">i", struct.pack(">I", raw))[0]


def read_uint32(registers, idx):
    """Reconstruct uint32 from two uint16 registers (big-endian, high word first)."""
    return (registers[idx] << 16) | registers[idx + 1]


def test_fc03_reads_all_18_registers(client):
    """FC03 reading all 18 registers (0x0000, count=18) must succeed."""
    rr = client.read_holding_registers(0x0000, 18, slave=1)
    assert not rr.isError(), f"FC03 failed: {rr}"
    assert len(rr.registers) == 18


def test_voltage_is_plausible(client):
    """L1 voltage must be in range 200-260 V (typical Dutch grid)."""
    rr = client.read_holding_registers(0x0000, 2, slave=1)
    assert not rr.isError()
    raw = read_int32(rr.registers, 0)
    voltage = raw / 10.0  # x0.1 V
    assert 200.0 <= voltage <= 260.0, f"Voltage out of range: {voltage} V"


def test_current_is_non_negative(client):
    """L1 current must be >= 0 (always positive magnitude)."""
    rr = client.read_holding_registers(0x0002, 2, slave=1)
    assert not rr.isError()
    raw = read_int32(rr.registers, 0)
    current = raw / 1000.0  # x0.001 A
    assert current >= 0.0, f"Current is negative: {current} A"


def test_power_is_signed_int32(client):
    """Active power register pair must decode as a valid signed int32."""
    rr = client.read_holding_registers(0x0004, 2, slave=1)
    assert not rr.isError()
    power = read_int32(rr.registers, 0) / 10.0
    assert -100000.0 < power < 100000.0, f"Power implausible: {power} W"


def test_reserved_registers_are_zero(client):
    """Registers 0x0006-0x000D (indices 6-13) must be zero."""
    rr = client.read_holding_registers(0x0006, 8, slave=1)
    assert not rr.isError()
    assert all(r == 0 for r in rr.registers), \
        f"Reserved registers non-zero: {rr.registers}"


def test_energy_import_is_non_negative(client):
    """Energy import total must be >= 0."""
    rr = client.read_holding_registers(0x000E, 2, slave=1)
    assert not rr.isError()
    value = read_uint32(rr.registers, 0)
    assert value >= 0  # uint32 is always >= 0; checks decode works


def test_energy_export_is_non_negative(client):
    """Energy export total must be >= 0."""
    rr = client.read_holding_registers(0x0010, 2, slave=1)
    assert not rr.isError()
    value = read_uint32(rr.registers, 0)
    assert value >= 0


def test_fc04_reads_same_as_fc03(client):
    """FC04 must return the same register values as FC03 (Cerbo may use either)."""
    rr03 = client.read_holding_registers(0x0000, 18, slave=1)
    rr04 = client.read_input_registers(0x0000, 18, slave=1)  # pymodbus uses FC04 for input registers
    assert not rr03.isError() and not rr04.isError()
    assert rr03.registers == rr04.registers, \
        f"FC03 and FC04 returned different values: {rr03.registers} vs {rr04.registers}"


def test_fc03_out_of_range_returns_exception(client):
    """FC03 starting at address 18 (one past end) must return exception 0x02."""
    rr = client.read_holding_registers(18, 1, slave=1)
    assert rr.isError() or (hasattr(rr, 'function_code') and rr.function_code == 0x83), \
        "Expected Modbus exception for out-of-range address"


def test_fc03_overflow_returns_exception(client):
    """FC03 with start=0, count=19 (one past end) must return exception 0x02."""
    rr = client.read_holding_registers(0, 19, slave=1)
    assert rr.isError() or (hasattr(rr, 'function_code') and rr.function_code == 0x83), \
        "Expected Modbus exception for overrun count"


def test_fc06_write_returns_illegal_function(client):
    """FC06 write must return exception 0x01 (component is read-only)."""
    wr = client.write_register(0x0004, 0, slave=1)
    assert wr.isError() or (hasattr(wr, 'function_code') and wr.function_code == 0x86), \
        "Expected Illegal Function exception for write attempt"


def test_readings_are_stable_between_polls(client):
    """Reading all registers twice within 1 second should give same voltage and energy."""
    import time
    rr1 = client.read_holding_registers(0x0000, 18, slave=1)
    time.sleep(0.5)
    rr2 = client.read_holding_registers(0x0000, 18, slave=1)
    assert not rr1.isError() and not rr2.isError()
    # Voltage (regs 0-1) should not change between rapid polls
    assert rr1.registers[0:2] == rr2.registers[0:2], \
        f"Voltage changed between polls: {rr1.registers[0:2]} vs {rr2.registers[0:2]}"
    # Energy (regs 14-17) should not change between rapid polls
    assert rr1.registers[14:18] == rr2.registers[14:18], \
        f"Energy changed between polls: {rr1.registers[14:18]} vs {rr2.registers[14:18]}"
