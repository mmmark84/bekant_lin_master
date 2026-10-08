import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_RX_PIN, CONF_TX_PIN

CODEOWNERS = []
DEPENDENCIES = ["esp32"]
# Platform headers are included unconditionally by the hub, so make sure they exist.
AUTO_LOAD = ["button", "cover", "number", "sensor", "text_sensor"]
MULTI_CONF = True

CONF_BEKANT_ID = "bekant_id"
CONF_CS_PIN = "cs_pin"
CONF_UART_NUM = "uart_num"
CONF_MIN_HEIGHT_RAW = "min_height_raw"
CONF_MAX_HEIGHT_RAW = "max_height_raw"
CONF_CALIBRATION = "calibration"
CONF_RAW_LOW = "raw_low"
CONF_CM_LOW = "cm_low"
CONF_RAW_HIGH = "raw_high"
CONF_CM_HIGH = "cm_high"
CONF_CYCLE_GAP = "cycle_gap"
CONF_MAX_DRIFT = "max_drift"

# Encoder range the original controller stays within (see bekant.h).
DANGER_MIN_RAW = 299
DANGER_MAX_RAW = 6640

bekant_ns = cg.esphome_ns.namespace("bekant")
BekantDesk = bekant_ns.class_("BekantDesk", cg.Component)

# Default calibration taken from the Megadesk companion; measure your own desk.
CALIBRATION_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_RAW_LOW, default=299): cv.int_range(min=0, max=65535),
        cv.Optional(CONF_CM_LOW, default=58.42): cv.float_,
        cv.Optional(CONF_RAW_HIGH, default=6640): cv.int_range(min=0, max=65535),
        cv.Optional(CONF_CM_HIGH, default=119.38): cv.float_,
    }
)


def _validate(config):
    if config[CONF_MIN_HEIGHT_RAW] >= config[CONF_MAX_HEIGHT_RAW]:
        raise cv.Invalid(f"{CONF_MIN_HEIGHT_RAW} must be below {CONF_MAX_HEIGHT_RAW}")
    calibration = config[CONF_CALIBRATION]
    if calibration[CONF_RAW_LOW] == calibration[CONF_RAW_HIGH]:
        raise cv.Invalid(f"{CONF_RAW_LOW} and {CONF_RAW_HIGH} must differ")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BekantDesk),
            cv.Required(CONF_TX_PIN): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_RX_PIN): pins.internal_gpio_input_pin_number,
            cv.Optional(CONF_CS_PIN): pins.internal_gpio_output_pin_number,
            cv.Optional(CONF_UART_NUM, default=1): cv.int_range(min=0, max=2),
            cv.Optional(CONF_MIN_HEIGHT_RAW, default=DANGER_MIN_RAW): cv.int_range(
                min=DANGER_MIN_RAW, max=DANGER_MAX_RAW
            ),
            cv.Optional(CONF_MAX_HEIGHT_RAW, default=DANGER_MAX_RAW): cv.int_range(
                min=DANGER_MIN_RAW, max=DANGER_MAX_RAW
            ),
            cv.Optional(CONF_CALIBRATION, default={}): CALIBRATION_SCHEMA,
            cv.Optional(CONF_CYCLE_GAP, default="50ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=20),
                    max=cv.TimePeriod(milliseconds=500),
                ),
            ),
            cv.Optional(CONF_MAX_DRIFT, default=200): cv.int_range(min=0, max=2000),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    _validate,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_uart_num(config[CONF_UART_NUM]))
    cg.add(var.set_tx_pin(config[CONF_TX_PIN]))
    cg.add(var.set_rx_pin(config[CONF_RX_PIN]))
    if CONF_CS_PIN in config:
        cg.add(var.set_cs_pin(config[CONF_CS_PIN]))
    cg.add(
        var.set_height_limits_raw(
            config[CONF_MIN_HEIGHT_RAW], config[CONF_MAX_HEIGHT_RAW]
        )
    )
    calibration = config[CONF_CALIBRATION]
    cg.add(
        var.set_calibration(
            calibration[CONF_RAW_LOW],
            calibration[CONF_CM_LOW],
            calibration[CONF_RAW_HIGH],
            calibration[CONF_CM_HIGH],
        )
    )
    cg.add(var.set_cycle_gap_ms(int(config[CONF_CYCLE_GAP].total_milliseconds)))
    cg.add(var.set_max_drift(config[CONF_MAX_DRIFT]))
