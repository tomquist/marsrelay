from __future__ import annotations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32
from esphome.const import CONF_ID, CONF_PORT, CONF_TRIGGER_ID

CODEOWNERS = ["@marsrelay"]
DEPENDENCIES = ["esp32"]

CONF_ON_MESSAGE = "on_message"
CONF_MAX_CLIENTS = "max_clients"
CONF_TLS = "tls"
CONF_TLS_SKIP_VERIFICATION = "tls_skip_verification"
CONF_ID_MAPPINGS = "id_mappings"
CONF_DEVICE = "device"
CONF_EXTERNAL = "external"

mosquitto_broker_ns = cg.esphome_ns.namespace("mosquitto_broker")
MosquittoBroker = mosquitto_broker_ns.class_("MosquittoBroker", cg.Component)
MosquittoMessageTrigger = mosquitto_broker_ns.class_(
    "MosquittoMessageTrigger", automation.Trigger.template(cg.std_string, cg.std_string)
)
PublishMessageAction = mosquitto_broker_ns.class_("PublishMessageAction", automation.Action)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MosquittoBroker),
            cv.Optional(CONF_TLS, default=True): cv.boolean,
            cv.Optional(CONF_TLS_SKIP_VERIFICATION, default=False): cv.boolean,
            cv.Optional(CONF_PORT): cv.port,  # Default will be set based on TLS
            cv.Optional(CONF_MAX_CLIENTS, default=10): cv.int_range(min=1, max=100),
            cv.Optional(CONF_ON_MESSAGE): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(MosquittoMessageTrigger),
                }
            ),
            cv.Optional(CONF_ID_MAPPINGS, default=[]): cv.ensure_list(
                cv.Schema(
                    {
                        cv.Required(CONF_DEVICE): cv.string_strict,
                        cv.Required(CONF_EXTERNAL): cv.string_strict,
                    }
                )
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set TLS first, then port (port default depends on TLS)
    cg.add(var.set_tls(config[CONF_TLS]))
    cg.add(var.set_tls_skip_verification(config[CONF_TLS_SKIP_VERIFICATION]))
    
    # Set port - use default based on TLS if not specified
    port = config.get(CONF_PORT)
    if port is None:
        port = 8883 if config[CONF_TLS] else 1883
    cg.add(var.set_port(port))
    
    cg.add(var.set_max_clients(config[CONF_MAX_CLIENTS]))

    esp32.add_idf_component(name="espressif/mosquitto", ref="2.0.20")

    for conf in config.get(CONF_ON_MESSAGE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        cg.add(var.add_message_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "topic"), (cg.std_string, "payload")], conf)

    for mapping in config.get(CONF_ID_MAPPINGS, []):
        cg.add(var.add_id_mapping(mapping[CONF_DEVICE], mapping[CONF_EXTERNAL]))


@automation.register_action(
    "mosquitto_broker.publish_message",
    PublishMessageAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(MosquittoBroker),
            cv.Required("topic"): cv.templatable(cv.string_strict),
            cv.Required("payload"): cv.templatable(cv.string_strict),
        }
    ),
    synchronous=True,
)
async def publish_message_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    topic = await cg.templatable(config["topic"], args, cg.std_string)
    payload = await cg.templatable(config["payload"], args, cg.std_string)
    cg.add(var.set_topic(topic))
    cg.add(var.set_payload(payload))
    return var
