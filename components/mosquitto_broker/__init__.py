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

# Topic segments that have structural meaning in the marsrelay bridge. If a
# mapping's `device` or `external` value collided with one of these, the
# translator would rewrite the structural segment and break the `/device/`
# filter the local->external bridge relies on, which can in turn enable a
# forwarding loop. Reject up front instead.
RESERVED_ID_SEGMENTS = frozenset(
    {"marstek_energy", "App", "device", "ctrl"}
)


def _validate_id_segment(value):
    value = cv.string_strict(value)
    if not value:
        raise cv.Invalid("ID must not be empty")
    if "/" in value:
        raise cv.Invalid("ID must not contain '/' (it is a single topic segment)")
    if "+" in value or "#" in value:
        raise cv.Invalid("ID must not contain MQTT wildcard characters ('+' or '#')")
    if value in RESERVED_ID_SEGMENTS:
        raise cv.Invalid(
            f"ID must not be one of the reserved topic segments: "
            f"{', '.join(sorted(RESERVED_ID_SEGMENTS))}"
        )
    return value


def _validate_id_mappings(value):
    seen_devices = set()
    seen_externals = set()
    for index, mapping in enumerate(value):
        device = mapping[CONF_DEVICE]
        external = mapping[CONF_EXTERNAL]
        if device == external:
            raise cv.Invalid(
                f"id_mappings[{index}]: 'device' and 'external' must differ "
                f"(both were '{device}')"
            )
        if device in seen_devices:
            raise cv.Invalid(
                f"id_mappings[{index}]: duplicate device id '{device}'"
            )
        if external in seen_externals:
            raise cv.Invalid(
                f"id_mappings[{index}]: duplicate external id '{external}'"
            )
        if device in seen_externals or external in seen_devices:
            raise cv.Invalid(
                f"id_mappings[{index}]: id '{device if device in seen_externals else external}' "
                f"is used on both sides across mappings, which would cause ambiguous translation"
            )
        seen_devices.add(device)
        seen_externals.add(external)
    return value


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
            cv.Optional(CONF_ID_MAPPINGS, default=[]): cv.All(
                cv.ensure_list(
                    cv.Schema(
                        {
                            cv.Required(CONF_DEVICE): _validate_id_segment,
                            cv.Required(CONF_EXTERNAL): _validate_id_segment,
                        }
                    )
                ),
                _validate_id_mappings,
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
