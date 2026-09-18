"""Diagnostic sensors for the local broker: what moved, and how long ago."""

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

from . import CONF_MOSQUITTO_BROKER_ID, MosquittoBroker

DEPENDENCIES = ["mosquitto_broker"]

CONF_DEVICE_MESSAGES = "device_messages"
CONF_APP_MESSAGES = "app_messages"
CONF_PUBLISH_ERRORS = "publish_errors"
CONF_BROKER_RESTARTS = "broker_restarts"
CONF_DEVICE_MESSAGE_AGE = "device_message_age"


def _counter_schema(icon: str) -> cv.Schema:
    # Counters reset on reboot, which is what total_increasing is for.
    return sensor.sensor_schema(
        icon=icon,
        accuracy_decimals=0,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MOSQUITTO_BROKER_ID): cv.use_id(MosquittoBroker),
        cv.Optional(CONF_DEVICE_MESSAGES): _counter_schema("mdi:battery-arrow-up"),
        cv.Optional(CONF_APP_MESSAGES): _counter_schema("mdi:battery-arrow-down"),
        cv.Optional(CONF_PUBLISH_ERRORS): _counter_schema("mdi:alert-circle-outline"),
        cv.Optional(CONF_BROKER_RESTARTS): _counter_schema("mdi:restart-alert"),
        cv.Optional(CONF_DEVICE_MESSAGE_AGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            icon="mdi:timer-sand",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DURATION,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)

_SETTERS = {
    CONF_DEVICE_MESSAGES: "set_device_messages_sensor",
    CONF_APP_MESSAGES: "set_app_messages_sensor",
    CONF_PUBLISH_ERRORS: "set_publish_errors_sensor",
    CONF_BROKER_RESTARTS: "set_broker_restarts_sensor",
    CONF_DEVICE_MESSAGE_AGE: "set_device_message_age_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MOSQUITTO_BROKER_ID])
    for key, setter in _SETTERS.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(parent, setter)(sens))
