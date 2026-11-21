"""Keeps the WiFi access point up next to the station for Marsrelay.

The batteries and meters that join the Marsrelay's access point need it after
the station connects, but ESPHome's default AP is a fallback that shuts down as
soon as the station associates. Every Marsrelay component auto-loads this one,
which switches the wifi component to ap_mode: ALWAYS_ON. Configs users already
downloaded get it that way, without a change on their side.
"""

import logging

import esphome.config_validation as cv
from esphome.const import __version__ as ESPHOME_VERSION
from esphome.core import CORE
import esphome.final_validate as fv

CODEOWNERS = ["@marsrelay"]

_LOGGER = logging.getLogger(__name__)

# The first release whose wifi component can keep the AP up next to the station
MIN_ESPHOME_VERSION = cv.Version(2026, 11, 0)

CONFIG_SCHEMA = cv.Schema({})


def _final_validate(config):
    wifi = fv.full_config.get().get("wifi")
    # ap_mode: ALWAYS_ON is ESP32-only, and without an AP there is nothing to keep up
    if wifi is None or "ap" not in wifi or not CORE.is_esp32:
        return config
    if cv.Version.parse(ESPHOME_VERSION) < MIN_ESPHOME_VERSION:
        raise cv.Invalid(
            f"Marsrelay needs ESPHome {MIN_ESPHOME_VERSION} or newer to keep its "
            f"access point up next to the WiFi connection (this is {ESPHOME_VERSION})"
        )
    ap = wifi["ap"]
    # ALWAYS_ON, or WISP, already keeps it up. Without a station network the
    # fallback AP is all there is, and ALWAYS_ON would be rejected.
    if ap["ap_mode"] != "FALLBACK" or not wifi.get("networks"):
        return config
    # Runs after the wifi schema has filled in its defaults, so an explicit
    # ap_mode: FALLBACK can't be told apart from the default, and is changed too.
    _LOGGER.info(
        "Marsrelay keeps the WiFi AP up next to the station "
        "(ap_mode: ALWAYS_ON, post_connect_roaming: false)"
    )
    ap["ap_mode"] = "ALWAYS_ON"
    # ALWAYS_ON requires it: the roaming scan would take the AP and the devices
    # on it off the air.
    wifi["post_connect_roaming"] = False
    return config


FINAL_VALIDATE_SCHEMA = _final_validate
