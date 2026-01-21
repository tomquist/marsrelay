from __future__ import annotations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID, CONF_TRIGGER_ID

CONF_ON_REQUEST = "on_request"

AUTO_LOAD = ["web_server_base"]

marstack_webserver_ns = cg.esphome_ns.namespace("marstack_webserver")
MarstackWebServer = marstack_webserver_ns.class_("MarstackWebServer", cg.Component)
MarstackRequestTrigger = marstack_webserver_ns.class_(
    "MarstackRequestTrigger",
    automation.Trigger.template(
        cg.std_string, cg.std_string, cg.std_string, cg.std_string
    ),
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MarstackWebServer),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
            web_server_base.WebServerBase
        ),
        cv.Optional(CONF_ON_REQUEST): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(MarstackRequestTrigger),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await cg.register_component(var, config)
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
