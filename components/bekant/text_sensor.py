import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_STATUS, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_BEKANT_ID, BekantDesk

DEPENDENCIES = ["bekant"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BEKANT_ID): cv.use_id(BekantDesk),
        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
            icon="mdi:information-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    desk = await cg.get_variable(config[CONF_BEKANT_ID])
    if status_config := config.get(CONF_STATUS):
        sens = await text_sensor.new_text_sensor(status_config)
        cg.add(desk.set_status_text_sensor(sens))
