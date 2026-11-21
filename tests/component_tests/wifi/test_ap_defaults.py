from pathlib import Path

from esphome.core import CORE

from tests.component_tests.conftest import generate_main


# Sets wifi.ap without ap_mode or post_connect_roaming, like the shipped config.
FIXTURE = Path(__file__).parents[3] / "tests" / "fixtures" / "udp_proxy.yaml"


def test_marsrelay_keeps_the_ap_up_next_to_the_station(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # The batteries and meters on the AP need it after the station connects, so
    # every Marsrelay component switches ESPHome's wifi to ap_mode: ALWAYS_ON
    # instead of its default FALLBACK.
    assert "marsrelay_ap" in CORE.loaded_integrations
    assert CORE.config["wifi"]["ap"]["ap_mode"] == "ALWAYS_ON"
    assert any(str(d).startswith("USE_WIFI_AP_PERSISTENT") for d in CORE.defines)
    # ALWAYS_ON requires the roaming scan off, so that is switched off too.
    assert CORE.config["wifi"]["post_connect_roaming"] is False
    assert "set_post_connect_roaming(false)" in main_cpp
