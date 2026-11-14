from pathlib import Path

from tests.component_tests.conftest import generate_main


FIXTURE = Path(__file__).parents[3] / "tests" / "fixtures" / "udp_proxy.yaml"


def test_destination_network_reaches_the_component(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    assert "meter_on_home->set_meter_on_ap(false)" in main_cpp
    assert "meter_on_ap->set_meter_on_ap(true)" in main_cpp


def test_meter_network_still_works_with_a_warning(generate_main, caplog):
    main_cpp = generate_main(str(FIXTURE))
    assert "legacy_meter_on_ap->set_meter_on_ap(true)" in main_cpp
    assert "'meter_network' is deprecated, use 'destination_network'" in caplog.text


def test_destination_reaches_the_component(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    assert "meter_on_home->set_destination(network::IPAddress(192, 168, 1, 50))" in main_cpp
    assert "meter_on_ap->set_destination" not in main_cpp
