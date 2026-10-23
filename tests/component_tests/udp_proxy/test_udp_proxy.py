from pathlib import Path

from tests.component_tests.conftest import generate_main


FIXTURE = Path(__file__).parents[3] / "tests" / "fixtures" / "udp_proxy_meter_network.yaml"


def test_meter_network_reaches_the_component(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    assert "meter_on_home->set_meter_on_ap(false)" in main_cpp
    assert "meter_on_ap->set_meter_on_ap(true)" in main_cpp
