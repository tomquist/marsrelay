from pathlib import Path

import pytest

from tests.component_tests.conftest import generate_main

REPO_ROOT = Path(__file__).parents[3]
REMOTE_SOURCE = "source: github://tomquist/marsrelay@main"


@pytest.fixture
def root_config(tmp_path: Path) -> str:
    """The shipped config, with its GitHub source swapped for this checkout.

    Users fetch the components from GitHub; the tests must exercise the working
    tree instead, or they would be testing main.
    """
    config = (REPO_ROOT / "marsrelay_esp32s3.yaml").read_text()
    assert REMOTE_SOURCE in config, "shipped config no longer uses the GitHub source"
    config = config.replace(
        REMOTE_SOURCE,
        f"source: {{type: local, path: {REPO_ROOT / 'components'}}}",
    )
    path = tmp_path / "marsrelay_esp32s3.yaml"
    path.write_text(config)
    return str(path)


def test_marsrelay_component_generation(generate_main, root_config):
    main_cpp = generate_main(root_config)
    assert "mosquitto_broker::MosquittoBroker" in main_cpp
    assert "marstack::Marstack" in main_cpp
    assert "capture_dns::CaptiveDns" in main_cpp
    assert "udp_proxy::UdpProxy" in main_cpp


def test_marsrelay_ships_diagnostic_entities(generate_main, root_config):
    """The example config wires up the diagnostics, not just the components."""
    main_cpp = generate_main(root_config)
    # Bound to the component ids the config declares: set_device_active_binary_sensor
    # is emitted by both mosquitto_broker and marstack, so a bare substring
    # check would pass with either one missing.
    for call in (
        "local_broker->set_running_binary_sensor",
        "local_broker->set_device_active_binary_sensor",
        "local_broker->set_device_messages_sensor",
        "local_broker->set_device_message_age_sensor",
        "meter_proxy->set_active_binary_sensor",
        "meter_proxy->set_meter_responding_binary_sensor",
        "meter_proxy->set_packets_to_sta_sensor",
        "marstack_http->set_device_active_binary_sensor",
    ):
        assert f"{call}(" in main_cpp, f"missing {call}"
