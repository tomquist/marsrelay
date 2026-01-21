from pathlib import Path

from tests.component_tests.conftest import generate_main


def test_marsrelay_component_generation(generate_main):
    root_config = Path(__file__).parents[3] / "marsrelay_esp32s3.yaml"
    main_cpp = generate_main(str(root_config))
    assert "mosquitto_broker::MosquittoBroker" in main_cpp
    assert "marstack_webserver::MarstackWebServer" in main_cpp
    assert "captive_dns::CaptiveDns" in main_cpp
