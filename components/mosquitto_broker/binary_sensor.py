"""Diagnostic binary sensors for the local broker.

Answers "is the broker up, and is the battery still talking to it?" -- the two
questions a relay that looks healthy from the outside cannot otherwise answer.
"""

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TIMEOUT,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_RUNNING,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_MOSQUITTO_BROKER_ID, MosquittoBroker

DEPENDENCIES = ["mosquitto_broker"]

CONF_RUNNING = "running"
CONF_DEVICE_ACTIVE = "device_active"
CONF_PUBLISH_CLIENT_CONNECTED = "publish_client_connected"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MOSQUITTO_BROKER_ID): cv.use_id(MosquittoBroker),
        cv.Optional(CONF_RUNNING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:server-network",
        ),
        cv.Optional(CONF_DEVICE_ACTIVE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:battery-sync",
        ).extend(
            {
                # Batteries publish in bursts minutes apart, so a timeout this
                # side of a quarter hour would flap on a healthy setup.
                cv.Optional(
                    CONF_TIMEOUT, default="15min"
                ): cv.positive_time_period_milliseconds,
            }
        ),
        cv.Optional(
            CONF_PUBLISH_CLIENT_CONNECTED
        ): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:transit-connection-variant",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MOSQUITTO_BROKER_ID])

    if running_config := config.get(CONF_RUNNING):
        sens = await binary_sensor.new_binary_sensor(running_config)
        cg.add(parent.set_running_binary_sensor(sens))

    if device_config := config.get(CONF_DEVICE_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(device_config)
        cg.add(parent.set_device_active_binary_sensor(sens))
        cg.add(
            parent.set_device_active_timeout(
                device_config[CONF_TIMEOUT].total_milliseconds
            )
        )

    if client_config := config.get(CONF_PUBLISH_CLIENT_CONNECTED):
        sens = await binary_sensor.new_binary_sensor(client_config)
        cg.add(parent.set_publish_client_connected_binary_sensor(sens))
