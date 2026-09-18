"""Diagnostic sensors for the UDP proxy."""

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_DURATION,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_SECOND,
)

from . import CONF_UDP_PROXY_ID, UdpProxy

DEPENDENCIES = ["udp_proxy"]

CONF_PACKETS_TO_STA = "packets_to_sta"
CONF_PACKETS_TO_AP = "packets_to_ap"
CONF_PACKETS_DROPPED = "packets_dropped"
CONF_SESSIONS = "sessions"
CONF_REQUEST_AGE = "request_age"
CONF_RESPONSE_AGE = "response_age"


def _counter_schema(icon: str) -> cv.Schema:
    return sensor.sensor_schema(
        icon=icon,
        accuracy_decimals=0,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


def _age_schema(icon: str) -> cv.Schema:
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_SECOND,
        icon=icon,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_DURATION,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_UDP_PROXY_ID): cv.use_id(UdpProxy),
        cv.Optional(CONF_PACKETS_TO_STA): _counter_schema("mdi:arrow-right-bold"),
        cv.Optional(CONF_PACKETS_TO_AP): _counter_schema("mdi:arrow-left-bold"),
        cv.Optional(CONF_PACKETS_DROPPED): _counter_schema("mdi:delete-variant"),
        cv.Optional(CONF_SESSIONS): sensor.sensor_schema(
            icon="mdi:account-network",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_REQUEST_AGE): _age_schema("mdi:timer-sand"),
        cv.Optional(CONF_RESPONSE_AGE): _age_schema("mdi:timer-sand"),
    }
)

_SETTERS = {
    CONF_PACKETS_TO_STA: "set_packets_to_sta_sensor",
    CONF_PACKETS_TO_AP: "set_packets_to_ap_sensor",
    CONF_PACKETS_DROPPED: "set_packets_dropped_sensor",
    CONF_SESSIONS: "set_sessions_sensor",
    CONF_REQUEST_AGE: "set_request_age_sensor",
    CONF_RESPONSE_AGE: "set_response_age_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_UDP_PROXY_ID])
    for key, setter in _SETTERS.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(parent, setter)(sens))
