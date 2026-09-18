from esphome import automation
import esphome.codegen as cg
from esphome.config_helpers import filter_source_files_from_platform
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_PORT,
    CONF_TIMEOUT,
    CONF_TRIGGER_ID,
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
MULTI_CONF = True

CONF_SESSION_TIMEOUT = "session_timeout"
CONF_UDP_PROXY_ID = "udp_proxy_id"
CONF_DIAGNOSTICS_INTERVAL = "diagnostics_interval"
CONF_ON_METER_TIMEOUT = "on_meter_timeout"
CONF_ON_METER_RECOVERED = "on_meter_recovered"

udp_proxy_ns = cg.esphome_ns.namespace("udp_proxy")
UdpProxy = udp_proxy_ns.class_("UdpProxy", cg.Component)
MeterLivenessTrigger = udp_proxy_ns.class_(
    "MeterLivenessTrigger", automation.Trigger.template()
)


def _liveness_automation(default_timeout: str):
    # Each automation carries its own timeout, so one config can warn at a
    # short silence and act at a longer one.
    return automation.validate_automation(
        {
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(MeterLivenessTrigger),
            cv.Optional(
                CONF_TIMEOUT, default=default_timeout
            ): cv.positive_time_period_milliseconds,
        }
    )


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(UdpProxy),
            cv.Required(CONF_PORT): cv.port,
            cv.Optional(CONF_SESSION_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_DIAGNOSTICS_INTERVAL, default="60s"): cv.update_interval,
            cv.Optional(CONF_ON_METER_TIMEOUT): _liveness_automation("5min"),
            cv.Optional(CONF_ON_METER_RECOVERED): _liveness_automation("5min"),
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
    cg.add(var.set_diagnostics_interval(config[CONF_DIAGNOSTICS_INTERVAL].total_milliseconds))

    for key, fire_on_timeout in (
        (CONF_ON_METER_TIMEOUT, True),
        (CONF_ON_METER_RECOVERED, False),
    ):
        for conf in config.get(key, []):
            trigger = cg.new_Pvariable(
                conf[CONF_TRIGGER_ID],
                conf[CONF_TIMEOUT].total_milliseconds,
                fire_on_timeout,
            )
            cg.add(var.add_liveness_trigger(trigger))
            await automation.build_automation(trigger, [], conf)


FILTER_SOURCE_FILES = filter_source_files_from_platform(
    {
        "udp_proxy.cpp": {
            PlatformFramework.ESP32_IDF,
        },
    }
)
