"""
Schema validation tests — run without hardware, requires esphome installed.
Run: pytest tests/test_schema.py -v
"""
import pytest
import sys
import os

# Make the component importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))


def _load_schema():
    """Load CONFIG_SCHEMA from the component __init__.py."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "grid_meter",
        os.path.join(os.path.dirname(__file__), '..', 'components', 'grid_meter', '__init__.py')
    )
    mod = importlib.util.load_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.CONFIG_SCHEMA


def test_all_sensor_keys_are_required():
    """CONFIG_SCHEMA must declare all eight sensor keys as Required."""
    import esphome.config_validation as cv
    schema = _load_schema()
    required_keys = [
        "power_import", "power_export", "voltage", "current",
        "energy_import_t1", "energy_import_t2",
        "energy_export_t1", "energy_export_t2",
    ]
    for key in required_keys:
        partial = {k: "some_sensor_id" for k in required_keys if k != key}
        with pytest.raises(cv.Invalid, match=key):
            schema(partial)


def test_valid_config_accepted():
    """CONFIG_SCHEMA must accept a dict with all eight sensor keys present."""
    import esphome.config_validation as cv
    schema = _load_schema()
    config = {
        "power_import":    "power_delivered",
        "power_export":    "power_returned",
        "voltage":         "voltage_l1",
        "current":         "current_l1",
        "energy_import_t1": "energy_delivered_tariff1",
        "energy_import_t2": "energy_delivered_tariff2",
        "energy_export_t1": "energy_returned_tariff1",
        "energy_export_t2": "energy_returned_tariff2",
    }
    result = schema(config)
    assert result is not None
