"""Diagnostic binary sensors for the UDP proxy."""

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TIMEOUT,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_RUNNING,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_UDP_PROXY_ID, UdpProxy

DEPENDENCIES = ["udp_proxy"]

CONF_ACTIVE = "active"
CONF_METER_RESPONDING = "meter_responding"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_UDP_PROXY_ID): cv.use_id(UdpProxy),
        cv.Optional(CONF_ACTIVE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:swap-horizontal",
        ),
        cv.Optional(CONF_METER_RESPONDING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:gauge",
        ).extend(
            {
                # The battery broadcasts every few seconds and the meter
                # answers each one, so shorter timeouts flap.
                cv.Optional(
                    CONF_TIMEOUT, default="5min"
                ): cv.positive_time_period_milliseconds,
            }
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_UDP_PROXY_ID])

    if active_config := config.get(CONF_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(active_config)
        cg.add(parent.set_active_binary_sensor(sens))

    if meter_config := config.get(CONF_METER_RESPONDING):
        sens = await binary_sensor.new_binary_sensor(meter_config)
        cg.add(parent.set_meter_responding_binary_sensor(sens))
        cg.add(parent.set_meter_timeout(meter_config[CONF_TIMEOUT].total_milliseconds))
