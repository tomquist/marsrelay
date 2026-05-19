from pathlib import Path

import pytest


FIXTURE = (
    Path(__file__).parents[3] / "tests" / "fixtures" / "mosquitto_broker_id_mappings.yaml"
)


def test_mosquitto_broker_id_mappings_generation(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    assert "mosquitto_broker::MosquittoBroker" in main_cpp
    assert "add_id_mapping(" in main_cpp
    assert "1111111111111111111111111111111111111111111111111111111111111111" in main_cpp
    assert "aabbccddeeff" in main_cpp
    assert "22222222222222222222222222222222" in main_cpp
    assert "aabbccddee00" in main_cpp


def _validate(mappings):
    from components.mosquitto_broker import _validate_id_mappings, _validate_id_segment

    normalised = []
    for mapping in mappings:
        normalised.append(
            {
                "device": _validate_id_segment(mapping["device"]),
                "external": _validate_id_segment(mapping["external"]),
            }
        )
    return _validate_id_mappings(normalised)


@pytest.mark.parametrize(
    "device, external, message",
    [
        ("device", "abcd", "reserved"),
        ("abcd", "App", "reserved"),
        ("ctrl", "abcd", "reserved"),
        ("marstek_energy", "abcd", "reserved"),
        ("", "abcd", "empty"),
        ("abcd", "", "empty"),
        ("ab/cd", "abcd", "'/'"),
        ("abcd", "ab+cd", "wildcard"),
        ("abcd", "ab#cd", "wildcard"),
    ],
)
def test_segment_rejected(device, external, message):
    from esphome.config_validation import Invalid

    with pytest.raises(Invalid, match=message):
        _validate([{"device": device, "external": external}])


def test_same_device_and_external_rejected():
    from esphome.config_validation import Invalid

    with pytest.raises(Invalid, match="must differ"):
        _validate([{"device": "same", "external": "same"}])


def test_duplicate_device_rejected():
    from esphome.config_validation import Invalid

    with pytest.raises(Invalid, match="duplicate device"):
        _validate(
            [
                {"device": "A", "external": "B"},
                {"device": "A", "external": "C"},
            ]
        )


def test_duplicate_external_rejected():
    from esphome.config_validation import Invalid

    with pytest.raises(Invalid, match="duplicate external"):
        _validate(
            [
                {"device": "A", "external": "X"},
                {"device": "B", "external": "X"},
            ]
        )


def test_id_used_on_both_sides_rejected():
    from esphome.config_validation import Invalid

    with pytest.raises(Invalid, match="both sides"):
        _validate(
            [
                {"device": "A", "external": "B"},
                {"device": "B", "external": "C"},
            ]
        )
