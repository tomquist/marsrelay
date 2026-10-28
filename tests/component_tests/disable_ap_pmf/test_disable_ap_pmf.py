from pathlib import Path

from esphome.core import CORE

from tests.component_tests.conftest import generate_main


FIXTURES = Path(__file__).parents[3] / "tests" / "fixtures"
WRAP = "-Wl,--wrap=esp_wifi_get_pmf_config_internal"
# Without it the linker never pulls the wrapper out of libsrc.a.
KEEP = "-Wl,--undefined=__wrap_esp_wifi_get_pmf_config_internal"


def test_wraps_the_pmf_config_getter(generate_main):
    main_cpp = generate_main(str(FIXTURES / "disable_ap_pmf.yaml"))
    assert "disable_ap_pmf::DisableApPmf()" in main_cpp
    assert WRAP in CORE.build_flags
    assert KEEP in CORE.build_flags


def test_pmf_untouched_without_it(generate_main):
    generate_main(str(FIXTURES / "udp_proxy_meter_network.yaml"))
    assert WRAP not in CORE.build_flags
    assert KEEP not in CORE.build_flags
