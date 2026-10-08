import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import CONF_BEKANT_ID, BekantDesk, bekant_ns

DEPENDENCIES = ["bekant"]

CONF_RECALIBRATE = "recalibrate"

BekantRecalibrateButton = bekant_ns.class_("BekantRecalibrateButton", button.Button)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BEKANT_ID): cv.use_id(BekantDesk),
        cv.Optional(CONF_RECALIBRATE): button.button_schema(
            BekantRecalibrateButton,
            icon="mdi:arrow-collapse-down",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
    }
)


async def to_code(config):
    desk = await cg.get_variable(config[CONF_BEKANT_ID])
    if recalibrate_config := config.get(CONF_RECALIBRATE):
        btn = await button.new_button(recalibrate_config)
        cg.add(btn.set_parent(desk))
