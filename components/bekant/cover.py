import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover

from . import CONF_BEKANT_ID, BekantDesk, bekant_ns

DEPENDENCIES = ["bekant"]

BekantCover = bekant_ns.class_("BekantCover", cover.Cover)

CONFIG_SCHEMA = cover.cover_schema(BekantCover, icon="mdi:desk").extend(
    {
        cv.GenerateID(CONF_BEKANT_ID): cv.use_id(BekantDesk),
    }
)


async def to_code(config):
    var = await cover.new_cover(config)
    desk = await cg.get_variable(config[CONF_BEKANT_ID])
    cg.add(var.set_parent(desk))
    cg.add(desk.set_cover(var))
