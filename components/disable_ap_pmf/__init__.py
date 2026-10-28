import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@marsrelay"]
DEPENDENCIES = ["esp32", "wifi"]

disable_ap_pmf_ns = cg.esphome_ns.namespace("disable_ap_pmf")
DisableApPmf = disable_ap_pmf_ns.class_("DisableApPmf", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.GenerateID(): cv.declare_id(DisableApPmf)}).extend(
        cv.COMPONENT_SCHEMA
    ),
    cv.only_on_esp32,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # ESP-IDF offers protected management frames on its access point whether
    # asked to or not. Its hostapd reads the access point's PMF settings through
    # this one function, so wrapping it is enough to switch PMF off there.
    # esp_wifi_disable_pmf_config() would too, but only between the wifi
    # component's esp_wifi_set_config() and esp_wifi_start().
    # --undefined: libsrc.a is scanned before the IDF library that calls it.
    cg.add_build_flag("-Wl,--wrap=esp_wifi_get_pmf_config_internal")
    cg.add_build_flag("-Wl,--undefined=__wrap_esp_wifi_get_pmf_config_internal")
