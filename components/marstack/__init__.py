from __future__ import annotations

import esphome.codegen as cg
from esphome.config_helpers import filter_source_files_from_platform
import esphome.config_validation as cv
from esphome import automation
from esphome.components import web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID, CONF_PORT, CONF_TRIGGER_ID, PlatformFramework
from esphome.core import CORE
import esphome.final_validate as fv
from esphome.types import ConfigType

CONF_MARSTACK_ID = "marstack_id"
CONF_DIAGNOSTICS_INTERVAL = "diagnostics_interval"
CONF_ON_REQUEST = "on_request"
CONF_ON_VENUS_UPLOAD = "on_venus_upload"
CONF_RAW_RESPONSES = "raw_responses"
CONF_TIME_SUFFIX = "time_suffix"
CONF_HTTPS = "https"
CONF_ACCEPT_ALL = "accept_all"
CONF_HOLD_TIME = "hold_time"
CONF_MAX_BODY = "max_body"
CONF_MAX_CONNECTIONS = "max_connections"

AUTO_LOAD = ["web_server_base", "json"]
CODEOWNERS = ["@marsrelay"]

marstack_ns = cg.esphome_ns.namespace("marstack")
Marstack = marstack_ns.class_("Marstack", cg.Component)
MarstackHttps = marstack_ns.class_("MarstackHttps", cg.Component)
MarstackRequestTrigger = marstack_ns.class_(
    "MarstackRequestTrigger",
    automation.Trigger.template(
        cg.std_string, cg.std_string, cg.std_string, cg.std_string
    ),
)
MarstackVenusUploadTrigger = marstack_ns.class_(
    "MarstackVenusUploadTrigger",
    automation.Trigger.template(cg.std_string, cg.std_string),
)

# The battery reads the TLS reply with a loop whose only early exit is our
# close_notify, so it waits out its own 20 s receive timeout on every upload.
# Closing before that makes it treat the error as a byte count and throw the
# reply away, which leaves the record in the upload buffer. Hold past 20 s.
_FIRMWARE_RECEIVE_TIMEOUT_MS = 20000


def _validate_hold_time(value):
    value = cv.positive_time_period_milliseconds(value)
    if 0 < value.total_milliseconds <= _FIRMWARE_RECEIVE_TIMEOUT_MS:
        raise cv.Invalid(
            f"hold_time must be longer than the battery's own "
            f"{_FIRMWARE_RECEIVE_TIMEOUT_MS // 1000}s receive timeout, or 0 to "
            f"close immediately. Closing while the battery is still reading "
            f"makes it discard the reply, which is what leaves telemetry "
            f"records in its upload buffer."
        )
    return value


HTTPS_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MarstackHttps),
        cv.Optional(CONF_PORT, default=443): cv.port,
        cv.Optional(CONF_ACCEPT_ALL, default=False): cv.boolean,
        cv.Optional(CONF_HOLD_TIME, default="25s"): _validate_hold_time,
        cv.Optional(CONF_MAX_BODY, default=8192): cv.int_range(min=512, max=65536),
        cv.Optional(CONF_MAX_CONNECTIONS, default=4): cv.int_range(min=1, max=16),
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Marstack),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
            web_server_base.WebServerBase
        ),
        cv.Optional(CONF_RAW_RESPONSES, default=True): cv.boolean,
        cv.Optional(CONF_TIME_SUFFIX, default="04_0_0_0"): cv.string_strict,
        cv.Optional(CONF_DIAGNOSTICS_INTERVAL, default="60s"): cv.update_interval,
        cv.Optional(CONF_HTTPS): HTTPS_SCHEMA,
        cv.Optional(CONF_ON_REQUEST): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(MarstackRequestTrigger),
            }
        ),
        cv.Optional(CONF_ON_VENUS_UPLOAD): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    MarstackVenusUploadTrigger
                ),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _final_validate(config: ConfigType) -> ConfigType:
    if CONF_HTTPS not in config:
        return config

    if not CORE.is_esp32 or CORE.target_framework != "esp-idf":
        raise cv.Invalid(
            "marstack 'https:' needs an ESP32 on the esp-idf framework: it "
            "serves TLS with esp-tls.",
            path=[CONF_HTTPS],
        )

    from esphome.components import socket

    https = config[CONF_HTTPS]
    # One listening socket, plus one per connection that may be held open
    # after being answered, plus the one currently being served.
    socket.consume_sockets(1, "marstack", socket.SocketType.TCP_LISTEN)(config)
    socket.consume_sockets(
        https[CONF_MAX_CONNECTIONS] + 1, "marstack", socket.SocketType.TCP
    )(config)

    web_server = fv.full_config.get().get("web_server")
    if web_server is not None and web_server.get(CONF_PORT) == https[CONF_PORT]:
        raise cv.Invalid(
            f"Port {https[CONF_PORT]} is already used by the 'web_server' "
            f"component; give one of them a different port.",
            path=[CONF_HTTPS, CONF_PORT],
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await cg.register_component(var, config)

    cg.add(var.set_raw_responses(config[CONF_RAW_RESPONSES]))
    cg.add(var.set_time_suffix(config[CONF_TIME_SUFFIX]))
    cg.add(
        var.set_diagnostics_interval(
            config[CONF_DIAGNOSTICS_INTERVAL].total_milliseconds
        )
    )

    for conf in config.get(CONF_ON_REQUEST, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        cg.add(var.add_request_trigger(trigger))
        await automation.build_automation(
            trigger,
            [
                (cg.std_string, "method"),
                (cg.std_string, "url"),
                (cg.std_string, "body"),
                (cg.std_string, "source_ip"),
            ],
            conf,
        )

    for conf in config.get(CONF_ON_VENUS_UPLOAD, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        cg.add(var.add_venus_upload_trigger(trigger))
        await automation.build_automation(
            trigger,
            [(cg.std_string, "device_id"), (cg.std_string, "telemetry")],
            conf,
        )

    if (https := config.get(CONF_HTTPS)) is not None:
        server = cg.new_Pvariable(https[CONF_ID], var)
        await cg.register_component(server, https)
        cg.add(server.set_port(https[CONF_PORT]))
        cg.add(server.set_accept_all(https[CONF_ACCEPT_ALL]))
        cg.add(server.set_hold_time(https[CONF_HOLD_TIME].total_milliseconds))
        cg.add(server.set_max_body(https[CONF_MAX_BODY]))
        cg.add(server.set_max_connections(https[CONF_MAX_CONNECTIONS]))


FILTER_SOURCE_FILES = filter_source_files_from_platform(
    {
        "marstack_https.cpp": {PlatformFramework.ESP32_IDF},
    }
)
