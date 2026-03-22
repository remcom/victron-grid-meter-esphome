import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

grid_meter_ns = cg.esphome_ns.namespace("grid_meter")
GridMeterComponent = grid_meter_ns.class_("GridMeterComponent", cg.Component)

CONF_POWER_IMPORT   = "power_import"
CONF_POWER_EXPORT   = "power_export"
CONF_VOLTAGE        = "voltage"
CONF_CURRENT        = "current"
CONF_ENERGY_IMP_T1  = "energy_import_t1"
CONF_ENERGY_IMP_T2  = "energy_import_t2"
CONF_ENERGY_EXP_T1  = "energy_export_t1"
CONF_ENERGY_EXP_T2  = "energy_export_t2"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GridMeterComponent),
        cv.Required(CONF_POWER_IMPORT):  cv.use_id(sensor.Sensor),
        cv.Required(CONF_POWER_EXPORT):  cv.use_id(sensor.Sensor),
        cv.Required(CONF_VOLTAGE):       cv.use_id(sensor.Sensor),
        cv.Required(CONF_CURRENT):       cv.use_id(sensor.Sensor),
        cv.Required(CONF_ENERGY_IMP_T1): cv.use_id(sensor.Sensor),
        cv.Required(CONF_ENERGY_IMP_T2): cv.use_id(sensor.Sensor),
        cv.Required(CONF_ENERGY_EXP_T1): cv.use_id(sensor.Sensor),
        cv.Required(CONF_ENERGY_EXP_T2): cv.use_id(sensor.Sensor),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    for conf_key, setter in [
        (CONF_POWER_IMPORT,  "set_power_import"),
        (CONF_POWER_EXPORT,  "set_power_export"),
        (CONF_VOLTAGE,       "set_voltage"),
        (CONF_CURRENT,       "set_current"),
        (CONF_ENERGY_IMP_T1, "set_energy_import_t1"),
        (CONF_ENERGY_IMP_T2, "set_energy_import_t2"),
        (CONF_ENERGY_EXP_T1, "set_energy_export_t1"),
        (CONF_ENERGY_EXP_T2, "set_energy_export_t2"),
    ]:
        sens = await cg.get_variable(config[conf_key])
        cg.add(getattr(var, setter)(sens))
