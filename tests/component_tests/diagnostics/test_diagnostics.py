from pathlib import Path

from tests.component_tests.conftest import generate_main


FIXTURE = Path(__file__).parents[3] / "tests" / "fixtures" / "diagnostics.yaml"

# Keyed by the component id the fixture declares, because several setter names
# are shared between components: both mosquitto_broker and marstack emit
# set_device_active_binary_sensor, and both udp_proxy and marstack emit
# set_request_age_sensor. A bare substring check would pass with one of them
# missing.
SETTERS = {
    "local_broker": (
        "set_running_binary_sensor",
        "set_device_active_binary_sensor",
        "set_publish_client_connected_binary_sensor",
        "set_device_messages_sensor",
        "set_app_messages_sensor",
        "set_publish_errors_sensor",
        "set_broker_restarts_sensor",
        "set_device_message_age_sensor",
    ),
    "meter_proxy": (
        "set_active_binary_sensor",
        "set_meter_responding_binary_sensor",
        "set_packets_to_sta_sensor",
        "set_packets_to_ap_sensor",
        "set_packets_dropped_sensor",
        "set_sessions_sensor",
        "set_request_age_sensor",
        "set_response_age_sensor",
    ),
    "marstack_http": (
        "set_device_active_binary_sensor",
        "set_requests_sensor",
        "set_venus_uploads_sensor",
        "set_request_age_sensor",
    ),
}


def test_diagnostic_entities_are_wired_up(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    for instance, setters in SETTERS.items():
        for setter in setters:
            assert f"{instance}->{setter}(" in main_cpp, f"missing {instance}->{setter}"


def test_diagnostic_timeouts_reach_the_components(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # The fixture overrides every timeout, so the defaults must not show up.
    assert "local_broker->set_device_active_timeout(1200000)" in main_cpp  # 20min
    assert "meter_proxy->set_meter_timeout(120000)" in main_cpp  # 2min
    assert "marstack_http->set_device_active_timeout(2700000)" in main_cpp  # 45min
    for instance in SETTERS:
        assert f"{instance}->set_diagnostics_interval(30000)" in main_cpp


def test_entities_are_diagnostic_by_default(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # The fixture defines no other entities, so every registration is one of
    # ours; codegen records the category in the trailing comment.
    registrations = [
        line
        for line in main_cpp.splitlines()
        if line.startswith(("App.register_sensor(", "App.register_binary_sensor("))
    ]
    assert len(registrations) == sum(len(s) for s in SETTERS.values())
    for line in registrations:
        assert "category:diagnostic" in line, line
