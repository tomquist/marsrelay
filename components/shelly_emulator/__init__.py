import esphome.codegen as cg
from esphome.config_helpers import filter_source_files_from_platform
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_PORT,
    PLATFORM_ESP32,
    PlatformFramework,
)
from esphome.core import CORE, coroutine_with_priority
from esphome.coroutine import CoroPriority
from esphome.types import ConfigType
from esphome.components import sensor


def AUTO_LOAD() -> list[str]:
    # Ensure required core components are loaded so their build flags/libs are available.
    # In particular, the ESPHome `json` component pulls in ArduinoJson for both Arduino and ESP-IDF.
    auto_load: list[str] = []
    if CORE.is_esp32:
        auto_load.extend(["socket", "json"])
    return auto_load


DEPENDENCIES = ["wifi", "json"]
CODEOWNERS = ["@marsrelay"]
MULTI_CONF = True

CONF_DEVICE_ID = "device_id"
CONF_POWER_SENSORS = "power_sensors"

shelly_emulator_ns = cg.esphome_ns.namespace("shelly_emulator")
ShellyEmulator = shelly_emulator_ns.class_("ShellyEmulator", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ShellyEmulator),
            cv.Optional(CONF_DEVICE_ID, default="marsrelay"): cv.string,
            cv.Required(CONF_PORT): cv.port,
            cv.Required(CONF_POWER_SENSORS): cv.All(
                cv.ensure_list(cv.use_id(sensor.Sensor)),
                cv.Length(min=1, max=3),
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on([PLATFORM_ESP32]),
)


@coroutine_with_priority(CoroPriority.CAPTIVE_PORTAL)
async def to_code(config: ConfigType):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add_define("USE_SHELLY_EMULATOR")

    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))

    for sens in config[CONF_POWER_SENSORS]:
        s = await cg.get_variable(sens)
        cg.add(var.add_power_sensor(s))


FILTER_SOURCE_FILES = filter_source_files_from_platform(
    {
        "shelly_emulator.cpp": {
            PlatformFramework.ESP32_IDF,
        },
    }
)
