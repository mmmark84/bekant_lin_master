import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_HEIGHT,
    DEVICE_CLASS_DISTANCE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_ARROW_EXPAND_VERTICAL,
    ICON_COUNTER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CENTIMETER,
)

from . import CONF_BEKANT_ID, BekantDesk

DEPENDENCIES = ["bekant"]

CONF_RAW_HEIGHT = "raw_height"
CONF_DRIFT = "drift"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BEKANT_ID): cv.use_id(BekantDesk),
        cv.Optional(CONF_HEIGHT): sensor.sensor_schema(
            unit_of_measurement=UNIT_CENTIMETER,
            icon=ICON_ARROW_EXPAND_VERTICAL,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_DISTANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_RAW_HEIGHT): sensor.sensor_schema(
            icon=ICON_COUNTER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_DRIFT): sensor.sensor_schema(
            icon="mdi:arrow-left-right",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    desk = await cg.get_variable(config[CONF_BEKANT_ID])
    if height_config := config.get(CONF_HEIGHT):
        sens = await sensor.new_sensor(height_config)
        cg.add(desk.set_height_sensor(sens))
    if raw_config := config.get(CONF_RAW_HEIGHT):
        sens = await sensor.new_sensor(raw_config)
        cg.add(desk.set_raw_height_sensor(sens))
    if drift_config := config.get(CONF_DRIFT):
        sens = await sensor.new_sensor(drift_config)
        cg.add(desk.set_drift_sensor(sens))
