from pathlib import Path

from tests.component_tests.conftest import generate_main


FIXTURE = Path(__file__).parents[3] / "tests" / "fixtures" / "diagnostics.yaml"

BROKER_SETTERS = (
    "set_running_binary_sensor",
    "set_device_active_binary_sensor",
    "set_publish_client_connected_binary_sensor",
    "set_device_messages_sensor",
    "set_app_messages_sensor",
    "set_publish_errors_sensor",
    "set_broker_restarts_sensor",
    "set_device_message_age_sensor",
)

UDP_PROXY_SETTERS = (
    "set_active_binary_sensor",
    "set_meter_responding_binary_sensor",
    "set_packets_to_sta_sensor",
    "set_packets_to_ap_sensor",
    "set_packets_dropped_sensor",
    "set_sessions_sensor",
    "set_request_age_sensor",
    "set_response_age_sensor",
)

MARSTACK_SETTERS = (
    "set_device_active_binary_sensor",
    "set_requests_sensor",
    "set_venus_uploads_sensor",
    "set_request_age_sensor",
)


def test_diagnostic_entities_are_wired_up(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    for setter in BROKER_SETTERS + UDP_PROXY_SETTERS + MARSTACK_SETTERS:
        assert f"{setter}(" in main_cpp, f"missing {setter}"


def test_diagnostic_timeouts_reach_the_components(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # The fixture overrides every timeout, so the defaults must not show up.
    assert "set_device_active_timeout(1200000)" in main_cpp  # broker, 20min
    assert "set_meter_timeout(120000)" in main_cpp  # udp_proxy, 2min
    assert "set_device_active_timeout(2700000)" in main_cpp  # marstack, 45min
    assert "set_diagnostics_interval(30000)" in main_cpp


def test_entities_are_diagnostic_by_default(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # Every entity here is plumbing, not something a dashboard should lead with.
    # The fixture defines no other entities, so every registration is one of
    # ours; codegen records the category in the trailing comment.
    registrations = [
        line
        for line in main_cpp.splitlines()
        if line.startswith(("App.register_sensor(", "App.register_binary_sensor("))
    ]
    assert len(registrations) == len(
        BROKER_SETTERS + UDP_PROXY_SETTERS + MARSTACK_SETTERS
    )
    for line in registrations:
        assert "category:diagnostic" in line, line
