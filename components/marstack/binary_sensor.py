"""Diagnostic binary sensor for the cloud HTTP emulation.

The battery polls these endpoints on a schedule of its own, independent of its
MQTT connection, so this keeps answering "is the battery there at all?" when
the MQTT side has gone quiet.
"""

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TIMEOUT,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_MARSTACK_ID, Marstack

DEPENDENCIES = ["marstack"]

CONF_DEVICE_ACTIVE = "device_active"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MARSTACK_ID): cv.use_id(Marstack),
        cv.Optional(CONF_DEVICE_ACTIVE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:cloud-check-outline",
        ).extend(
            {
                # How often a battery calls the clock endpoint varies by model
                # and firmware; half an hour is long enough not to flap.
                cv.Optional(
                    CONF_TIMEOUT, default="30min"
                ): cv.positive_time_period_milliseconds,
            }
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MARSTACK_ID])

    if device_config := config.get(CONF_DEVICE_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(device_config)
        cg.add(parent.set_device_active_binary_sensor(sens))
        cg.add(
            parent.set_device_active_timeout(
                device_config[CONF_TIMEOUT].total_milliseconds
            )
        )
