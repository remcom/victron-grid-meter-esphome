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
CONF_VOLTAGE_L1 = "voltage_l1"
CONF_CURRENT_L1 = "current_l1"
CONF_VOLTAGE_L2 = "voltage_l2"
CONF_CURRENT_L2 = "current_l2"
CONF_VOLTAGE_L3 = "voltage_l3"
CONF_CURRENT_L3 = "current_l3"
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
            cv.Required(CONF_VOLTAGE_L1): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CURRENT_L1): cv.use_id(sensor.Sensor),
            cv.Required(CONF_VOLTAGE_L2): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CURRENT_L2): cv.use_id(sensor.Sensor),
            cv.Required(CONF_VOLTAGE_L3): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CURRENT_L3): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ENERGY_IMP_T1): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ENERGY_IMP_T2): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ENERGY_EXP_T1): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ENERGY_EXP_T2): cv.use_id(sensor.Sensor),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


async def to_code(config):
    power_import = await cg.get_variable(config[CONF_POWER_IMPORT])
    power_export = await cg.get_variable(config[CONF_POWER_EXPORT])
    voltage_l1 = await cg.get_variable(config[CONF_VOLTAGE_L1])
    current_l1 = await cg.get_variable(config[CONF_CURRENT_L1])
    voltage_l2 = await cg.get_variable(config[CONF_VOLTAGE_L2])
    current_l2 = await cg.get_variable(config[CONF_CURRENT_L2])
    voltage_l3 = await cg.get_variable(config[CONF_VOLTAGE_L3])
    current_l3 = await cg.get_variable(config[CONF_CURRENT_L3])
    energy_import_t1 = await cg.get_variable(config[CONF_ENERGY_IMP_T1])
    energy_import_t2 = await cg.get_variable(config[CONF_ENERGY_IMP_T2])
    energy_export_t1 = await cg.get_variable(config[CONF_ENERGY_EXP_T1])
    energy_export_t2 = await cg.get_variable(config[CONF_ENERGY_EXP_T2])
    var = cg.new_Pvariable(
        config[CONF_ID],
        power_import,
        power_export,
        voltage_l1,
        current_l1,
        voltage_l2,
        current_l2,
        voltage_l3,
        current_l3,
        energy_import_t1,
        energy_import_t2,
        energy_export_t1,
        energy_export_t2,
    )
    await cg.register_component(var, config)
