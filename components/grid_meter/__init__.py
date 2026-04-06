import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

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
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_esp_idf,
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
    )
    await cg.register_component(var, config)
