"""Diagnostic sensors for the cloud HTTP emulation."""

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

from . import CONF_MARSTACK_ID, Marstack

DEPENDENCIES = ["marstack"]

CONF_REQUESTS = "requests"
CONF_VENUS_UPLOADS = "venus_uploads"
CONF_REQUEST_AGE = "request_age"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MARSTACK_ID): cv.use_id(Marstack),
        cv.Optional(CONF_REQUESTS): sensor.sensor_schema(
            icon="mdi:web",
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_VENUS_UPLOADS): sensor.sensor_schema(
            icon="mdi:cloud-upload-outline",
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_REQUEST_AGE): sensor.sensor_schema(
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
    CONF_REQUESTS: "set_requests_sensor",
    CONF_VENUS_UPLOADS: "set_venus_uploads_sensor",
    CONF_REQUEST_AGE: "set_request_age_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MARSTACK_ID])
    for key, setter in _SETTERS.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(parent, setter)(sens))
