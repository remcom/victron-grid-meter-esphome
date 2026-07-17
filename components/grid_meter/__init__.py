import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID, CONF_PORT

AUTO_LOAD = ["sensor"]
CODEOWNERS = []

grid_meter_ns = cg.esphome_ns.namespace("grid_meter")
GridMeterComponent = grid_meter_ns.class_("GridMeterComponent", cg.Component)

CONF_POWER_IMPORT = "power_import"
CONF_POWER_EXPORT = "power_export"
CONF_VOLTAGE = "voltage"
CONF_CURRENT = "current"
CONF_ENERGY_IMP_T1 = "energy_import_t1"
CONF_ENERGY_IMP_T2 = "energy_import_t2"
CONF_ENERGY_EXP_T1 = "energy_export_t1"
CONF_ENERGY_EXP_T2 = "energy_export_t2"
CONF_SERIAL_NUMBER = "serial_number"
CONF_DATA_TIMEOUT = "data_timeout"

# Optional per-phase keys; configuring a phase group enables 3-phase (3P.n) emulation.
# Keys are (voltage, current, power_import, power_export) per phase, passed to
# set_phase_sensors() with 0-based phase index 1 (L2) or 2 (L3).
PHASE_GROUPS = {
    1: ("voltage_l2", "current_l2", "power_import_l2", "power_export_l2"),
    2: ("voltage_l3", "current_l3", "power_import_l3", "power_export_l3"),
}


def _phase_schema():
    schema = {}
    for phase, keys in PHASE_GROUPS.items():
        group = f"phase_l{phase + 1}"
        for key in keys:
            schema[cv.Inclusive(key, group)] = cv.use_id(sensor.Sensor)
    return schema


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(GridMeterComponent),
            cv.Required(CONF_POWER_IMPORT): cv.use_id(sensor.Sensor),
            cv.Required(CONF_POWER_EXPORT): cv.use_id(sensor.Sensor),
            cv.Required(CONF_VOLTAGE): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CURRENT): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ENERGY_IMP_T1): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ENERGY_IMP_T2): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ENERGY_EXP_T1): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ENERGY_EXP_T2): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_PORT, default=502): cv.port,
            cv.Optional(CONF_SERIAL_NUMBER): cv.All(
                cv.string_strict, cv.Length(max=14)
            ),
            cv.Optional(
                CONF_DATA_TIMEOUT, default="30s"
            ): cv.positive_time_period_milliseconds,
            **_phase_schema(),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


async def to_code(config):
    power_import = await cg.get_variable(config[CONF_POWER_IMPORT])
    power_export = await cg.get_variable(config[CONF_POWER_EXPORT])
    voltage = await cg.get_variable(config[CONF_VOLTAGE])
    current = await cg.get_variable(config[CONF_CURRENT])
    energy_import_t1 = await cg.get_variable(config[CONF_ENERGY_IMP_T1])
    energy_import_t2 = await cg.get_variable(config[CONF_ENERGY_IMP_T2])
    energy_export_t1 = await cg.get_variable(config[CONF_ENERGY_EXP_T1])
    energy_export_t2 = await cg.get_variable(config[CONF_ENERGY_EXP_T2])
    var = cg.new_Pvariable(
        config[CONF_ID],
        power_import,
        power_export,
        voltage,
        current,
        energy_import_t1,
        energy_import_t2,
        energy_export_t1,
        energy_export_t2,
        config[CONF_PORT],
    )

    for phase, (v_key, c_key, pi_key, pe_key) in PHASE_GROUPS.items():
        if v_key in config:
            v = await cg.get_variable(config[v_key])
            c = await cg.get_variable(config[c_key])
            pi = await cg.get_variable(config[pi_key])
            pe = await cg.get_variable(config[pe_key])
            cg.add(var.set_phase_sensors(phase, v, c, pi, pe))

    if CONF_SERIAL_NUMBER in config:
        cg.add(var.set_serial_number(config[CONF_SERIAL_NUMBER]))
    cg.add(var.set_data_timeout(config[CONF_DATA_TIMEOUT].total_milliseconds))

    await cg.register_component(var, config)
