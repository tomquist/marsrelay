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
import esphome.final_validate as fv
from esphome.types import ConfigType


def AUTO_LOAD() -> list[str]:
    auto_load = []
    if CORE.is_esp32:
        auto_load.append("socket")
    return auto_load


DEPENDENCIES = ["wifi"]
CODEOWNERS = ["@marsrelay"]

CONF_SESSION_TIMEOUT = "session_timeout"

udp_proxy_ns = cg.esphome_ns.namespace("udp_proxy")
UdpProxy = udp_proxy_ns.class_("UdpProxy", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(UdpProxy),
            cv.Required(CONF_PORT): cv.port,
            cv.Optional(CONF_SESSION_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on([PLATFORM_ESP32]),
)


def _final_validate(config: ConfigType) -> ConfigType:
    full_config = fv.full_config.get()
    wifi_conf = full_config.get("wifi")

    if wifi_conf is None:
        raise cv.Invalid("UDP proxy requires the wifi component to be configured")

    if "ap" not in wifi_conf:
        raise cv.Invalid(
            "UDP proxy requires a WiFi AP to be configured. "
            "Add 'ap:' to your WiFi configuration."
        )

    if "ssid" not in wifi_conf and "networks" not in wifi_conf:
        raise cv.Invalid(
            "UDP proxy requires a WiFi STA connection (ssid or networks). "
            "The proxy bridges between AP and STA networks."
        )

    if CORE.is_esp32:
        from esphome.components import socket

        # We use 2 sockets: one for AP listening, one for STA forwarding
        socket.consume_sockets(2, "udp_proxy")(config)

    return config


FINAL_VALIDATE_SCHEMA = _final_validate


@coroutine_with_priority(CoroPriority.CAPTIVE_PORTAL)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add_define("USE_UDP_PROXY")

    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_session_timeout(config[CONF_SESSION_TIMEOUT]))


FILTER_SOURCE_FILES = filter_source_files_from_platform(
    {
        "udp_proxy.cpp": {
            PlatformFramework.ESP32_IDF,
        },
    }
)
