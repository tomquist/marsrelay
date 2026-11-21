from pathlib import Path

from esphome.core import CORE

from tests.component_tests.conftest import generate_main


# Sets wifi.ap without ap_mode or post_connect_roaming, like the shipped config.
FIXTURE = Path(__file__).parents[3] / "tests" / "fixtures" / "udp_proxy.yaml"


def test_ap_stays_up_next_to_the_station_by_default(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # The batteries and meters on the AP need it after the station connects,
    # so the fork defaults to ap_mode: ALWAYS_ON instead of upstream's FALLBACK.
    assert CORE.config["wifi"]["ap"]["ap_mode"] == "ALWAYS_ON"
    assert any(str(d).startswith("USE_WIFI_AP_PERSISTENT") for d in CORE.defines)
    # ALWAYS_ON requires the roaming scan off, so that defaults off too.
    assert CORE.config["wifi"]["post_connect_roaming"] is False
    assert "set_post_connect_roaming(false)" in main_cpp
