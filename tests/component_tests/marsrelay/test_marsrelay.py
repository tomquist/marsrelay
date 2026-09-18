from pathlib import Path

from tests.component_tests.conftest import generate_main


def test_marsrelay_component_generation(generate_main):
    root_config = Path(__file__).parents[3] / "marsrelay_esp32s3.yaml"
    main_cpp = generate_main(str(root_config))
    assert "mosquitto_broker::MosquittoBroker" in main_cpp
    assert "marstack::Marstack" in main_cpp
    assert "capture_dns::CaptiveDns" in main_cpp
    assert "udp_proxy::UdpProxy" in main_cpp


def test_marsrelay_ships_diagnostic_entities(generate_main):
    """The example config wires up the diagnostics, not just the components."""
    root_config = Path(__file__).parents[3] / "marsrelay_esp32s3.yaml"
    main_cpp = generate_main(str(root_config))
    for setter in (
        "set_running_binary_sensor",
        "set_device_active_binary_sensor",
        "set_active_binary_sensor",
        "set_meter_responding_binary_sensor",
        "set_device_messages_sensor",
        "set_device_message_age_sensor",
        "set_packets_to_sta_sensor",
    ):
        assert f"{setter}(" in main_cpp, f"missing {setter}"
