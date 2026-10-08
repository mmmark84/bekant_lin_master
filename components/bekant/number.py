import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_STEP,
    DEVICE_CLASS_DISTANCE,
    ICON_ARROW_EXPAND_VERTICAL,
    UNIT_CENTIMETER,
)

from . import CONF_BEKANT_ID, BekantDesk, bekant_ns

DEPENDENCIES = ["bekant"]

CONF_TARGET_HEIGHT = "target_height"

BekantTargetHeightNumber = bekant_ns.class_("BekantTargetHeightNumber", number.Number)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_BEKANT_ID): cv.use_id(BekantDesk),
        cv.Optional(CONF_TARGET_HEIGHT): number.number_schema(
            BekantTargetHeightNumber,
            icon=ICON_ARROW_EXPAND_VERTICAL,
            device_class=DEVICE_CLASS_DISTANCE,
            unit_of_measurement=UNIT_CENTIMETER,
        ).extend(
            {
                cv.Optional(CONF_MIN_VALUE, default=60.0): cv.float_,
                cv.Optional(CONF_MAX_VALUE, default=125.0): cv.float_,
                cv.Optional(CONF_STEP, default=0.5): cv.positive_float,
            }
        ),
    }
)


async def to_code(config):
    desk = await cg.get_variable(config[CONF_BEKANT_ID])
    if target_config := config.get(CONF_TARGET_HEIGHT):
        num = await number.new_number(
            target_config,
            min_value=target_config[CONF_MIN_VALUE],
            max_value=target_config[CONF_MAX_VALUE],
            step=target_config[CONF_STEP],
        )
        cg.add(num.set_parent(desk))
        cg.add(desk.set_target_height_number(num))
