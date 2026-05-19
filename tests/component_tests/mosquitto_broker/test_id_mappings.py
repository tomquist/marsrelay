from pathlib import Path


def test_mosquitto_broker_id_mappings_generation(generate_main):
    fixture = (
        Path(__file__).parents[3] / "tests" / "fixtures" / "mosquitto_broker_id_mappings.yaml"
    )
    main_cpp = generate_main(str(fixture))
    assert "mosquitto_broker::MosquittoBroker" in main_cpp
    assert "add_id_mapping(" in main_cpp
    assert "1111111111111111111111111111111111111111111111111111111111111111" in main_cpp
    assert "aabbccddeeff" in main_cpp
    assert "22222222222222222222222222222222" in main_cpp
    assert "aabbccddee00" in main_cpp
