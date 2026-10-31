from __future__ import annotations

import esphome.codegen as cg
from esphome.config_helpers import filter_source_files_from_platform
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import (
    CONF_ID,
    CONF_PORT,
    CONF_TIMEOUT,
    CONF_TRIGGER_ID,
    PlatformFramework,
)
from esphome.core import CORE
import esphome.final_validate as fv
from esphome.types import ConfigType

CONF_MARSTACK_ID = "marstack_id"
CONF_DIAGNOSTICS_INTERVAL = "diagnostics_interval"
CONF_ON_REQUEST = "on_request"
CONF_ON_DEVICE_TIMEOUT = "on_device_timeout"
CONF_ON_DEVICE_RECOVERED = "on_device_recovered"
CONF_ON_VENUS_UPLOAD = "on_venus_upload"
CONF_RAW_RESPONSES = "raw_responses"
CONF_TIME_SUFFIX = "time_suffix"
CONF_HTTPS = "https"
CONF_ACCEPT_ALL = "accept_all"
CONF_HOLD_TIME = "hold_time"
CONF_MAX_BODY = "max_body"
CONF_MAX_CONNECTIONS = "max_connections"
CONF_POWER_LIMITS = "power_limits"
CONF_MAX_POWER = "max_power"
CONF_MIN_POWER = "min_power"
CONF_POWER_VERSION = "power_version"

# B2500 firmware caps its maximum output power here whatever it is told.
_B2500_MAX_OUTPUT_W = 800

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
DeviceLivenessTrigger = marstack_ns.class_(
    "DeviceLivenessTrigger", automation.Trigger.template()
)


def _liveness_automation(default_timeout: str):
    # Each automation carries its own timeout, so one config can warn at a
    # short silence and act at a longer one.
    return automation.validate_automation(
        {
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(DeviceLivenessTrigger),
            cv.Optional(
                CONF_TIMEOUT, default=default_timeout
            ): cv.positive_time_period_milliseconds,
        }
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

def _validate_power_limits(config):
    if config.get(CONF_MIN_POWER, 0) > config[CONF_MAX_POWER]:
        raise cv.Invalid(
            f"min_power ({config[CONF_MIN_POWER]} W) must not exceed max_power "
            f"({config[CONF_MAX_POWER]} W)",
            path=[CONF_MIN_POWER],
        )
    return config


# What a B2500 is told when it asks for its output power limits. Leaving out
# min_power lets the battery pick its own (80 W, or 40 W on some variants).
# A battery that already has settings stored does not ask; see the README.
POWER_LIMITS_SCHEMA = cv.All(
    cv.Schema(
        {
            # 0 is what the firmware treats as "no settings stored".
            cv.Optional(CONF_MAX_POWER, default=_B2500_MAX_OUTPUT_W): cv.int_range(
                min=1, max=_B2500_MAX_OUTPUT_W
            ),
            cv.Optional(CONF_MIN_POWER): cv.int_range(min=0, max=_B2500_MAX_OUTPUT_W),
        }
    ),
    _validate_power_limits,
)

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
        cv.Optional(CONF_POWER_LIMITS): POWER_LIMITS_SCHEMA,
        # What a Venus E gen 1/2 is told its power class is when the app sends
        # "active upgrade". It stores this for good and resets its maximum
        # discharge power to it, so there is no default: unset, the request
        # goes unanswered.
        cv.Optional(CONF_POWER_VERSION): cv.one_of(800, 2500, int=True),
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
        cv.Optional(CONF_ON_DEVICE_TIMEOUT): _liveness_automation("30min"),
        cv.Optional(CONF_ON_DEVICE_RECOVERED): _liveness_automation("30min"),
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


def _request_tls_server():
    """Keep mbedTLS able to accept TLS connections.

    From ESPHome 2026.10 the build trims mbedTLS to what components request:
    TLS compiles out entirely without a request, and is cut to the client role
    and stripped of the legacy handshake extras with one. A server built like
    that fails every handshake (mbedtls_ssl_handshake returns -0x7100). The
    batteries are TLS clients we cannot vet, so keep the extras too, as on
    earlier releases. Older ESPHome leaves mbedTLS at the IDF defaults and has
    none of these helpers.
    """
    if not hasattr(esp32, "require_mbedtls_tls_server"):
        return
    esp32.request_tls()
    esp32.require_mbedtls_tls_server()
    esp32.require_mbedtls_tls_extras()


# The B2500 (HMJ firmware V116 at least) reports to
# `GET /prod/api/v1/setB2500Report?v=<encrypted report>` about once a minute,
# with a request line of up to ~1 KB. ESP-IDF's web server rejects anything
# longer than 512 bytes with 414 before marstack sees it, and a battery whose
# reports keep failing resets its own WiFi and loses its MQTT session for good.
_HTTPD_MIN_LIMIT = 2048
_HTTPD_LIMIT_OPTIONS = ("CONFIG_HTTPD_MAX_URI_LEN", "CONFIG_HTTPD_MAX_REQ_HDR_LEN")


def _raise_httpd_limits():
    """Raise the web server's request line and header limits to at least
    _HTTPD_MIN_LIMIT, keeping a larger value set in `sdkconfig_options`."""
    options = CORE.data[esp32.KEY_ESP32][esp32.KEY_SDKCONFIG_OPTIONS]
    for name in _HTTPD_LIMIT_OPTIONS:
        value = options.get(name, 0)
        # A user's sdkconfig_options value arrives wrapped in RawSdkconfigValue.
        value = getattr(value, "value", value)
        try:
            current = int(str(value).strip('"'))
        except ValueError:
            current = 0
        if current < _HTTPD_MIN_LIMIT:
            esp32.add_idf_sdkconfig_option(name, _HTTPD_MIN_LIMIT)


async def to_code(config):
    if CORE.is_esp32 and CORE.target_framework == "esp-idf":
        _raise_httpd_limits()
        # ESPHome starts the web server task with a fixed stack sized for the
        # default URI limit, and keeps a URI-sized buffer on it; wrap
        # httpd_start() to give the task room for the larger one (see
        # marstack.cpp). --undefined keeps the wrapper, which lives in libsrc.a,
        # scanned before the IDF library that calls httpd_start().
        cg.add_define("USE_MARSTACK_HTTPD_STACK")
        cg.add_build_flag("-Wl,--wrap=httpd_start")
        cg.add_build_flag("-Wl,--undefined=__wrap_httpd_start")
    parent = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await cg.register_component(var, config)

    cg.add(var.set_raw_responses(config[CONF_RAW_RESPONSES]))
    cg.add(var.set_time_suffix(config[CONF_TIME_SUFFIX]))
    if (limits := config.get(CONF_POWER_LIMITS)) is not None:
        cg.add(
            var.set_power_limits(
                limits[CONF_MAX_POWER], limits.get(CONF_MIN_POWER, -1)
            )
        )
    if (power_version := config.get(CONF_POWER_VERSION)) is not None:
        cg.add(var.set_power_version(power_version))
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

    for key, fire_on_timeout in (
        (CONF_ON_DEVICE_TIMEOUT, True),
        (CONF_ON_DEVICE_RECOVERED, False),
    ):
        for conf in config.get(key, []):
            trigger = cg.new_Pvariable(
                conf[CONF_TRIGGER_ID],
                conf[CONF_TIMEOUT].total_milliseconds,
                fire_on_timeout,
            )
            cg.add(var.add_liveness_trigger(trigger))
            await automation.build_automation(trigger, [], conf)

    if (https := config.get(CONF_HTTPS)) is not None:
        server = cg.new_Pvariable(https[CONF_ID], var)
        await cg.register_component(server, https)
        cg.add(server.set_port(https[CONF_PORT]))
        cg.add(server.set_accept_all(https[CONF_ACCEPT_ALL]))
        cg.add(server.set_hold_time(https[CONF_HOLD_TIME].total_milliseconds))
        cg.add(server.set_max_body(https[CONF_MAX_BODY]))
        cg.add(server.set_max_connections(https[CONF_MAX_CONNECTIONS]))
        _request_tls_server()


FILTER_SOURCE_FILES = filter_source_files_from_platform(
    {
        "marstack_https.cpp": {PlatformFramework.ESP32_IDF},
    }
)
