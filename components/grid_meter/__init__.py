import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

AUTO_LOAD = ["sensor"]
CODEOWNERS = []

grid_meter_ns = cg.esphome_ns.namespace("grid_meter")
GridMeterComponent = grid_meter_ns.class_("GridMeterComponent", cg.Component)

CONF_POWER_L1_IMPORT = "power_l1_import"
CONF_POWER_L1_EXPORT = "power_l1_export"
CONF_POWER_L2_IMPORT = "power_l2_import"
CONF_POWER_L2_EXPORT = "power_l2_export"
CONF_POWER_L3_IMPORT = "power_l3_import"
CONF_POWER_L3_EXPORT = "power_l3_export"
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
CONF_PHASE_CONFIG = "phase_config"  # Add new configuration constant

# Define valid values for phase_config
PHASE_CONFIG_VALUES = [0, 3]

def validate_phase_config(value):
    """Validate that phase_config is either 0 or 3."""
    if value not in PHASE_CONFIG_VALUES:
        raise cv.Invalid(f"phase_config must be one of {PHASE_CONFIG_VALUES}")
    return value

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(GridMeterComponent),
            cv.Required(CONF_POWER_L1_IMPORT): cv.use_id(sensor.Sensor),
            cv.Required(CONF_POWER_L1_EXPORT): cv.use_id(sensor.Sensor),
            cv.Required(CONF_POWER_L2_IMPORT): cv.use_id(sensor.Sensor),
            cv.Required(CONF_POWER_L2_EXPORT): cv.use_id(sensor.Sensor),
            cv.Required(CONF_POWER_L3_IMPORT): cv.use_id(sensor.Sensor),
            cv.Required(CONF_POWER_L3_EXPORT): cv.use_id(sensor.Sensor),
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
            cv.Optional(CONF_PHASE_CONFIG, default=0): validate_phase_config,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


async def to_code(config):
    power_l1_import = await cg.get_variable(config[CONF_POWER_L1_IMPORT])
    power_l1_export = await cg.get_variable(config[CONF_POWER_L1_EXPORT])
    power_l2_import = await cg.get_variable(config[CONF_POWER_L2_IMPORT])
    power_l2_export = await cg.get_variable(config[CONF_POWER_L2_EXPORT])
    power_l3_import = await cg.get_variable(config[CONF_POWER_L3_IMPORT])
    power_l3_export = await cg.get_variable(config[CONF_POWER_L3_EXPORT])
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

    # Get the integer configuration value (validated to be 0 or 3)
    phase_config = config[CONF_PHASE_CONFIG]IG])
        
    var = cg.new_Pvariable(
        config[CONF_ID],
        power_l1_import,
        power_l1_export,
        power_l2_import,
        power_l2_export,
        power_l3_import,
        power_l3_export,
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
        phase_config,  # Pass the new integer value to the constructor
    )
    await cg.register_component(var, config)
