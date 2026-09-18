from pathlib import Path

from tests.component_tests.conftest import generate_main


FIXTURE = Path(__file__).parents[3] / "tests" / "fixtures" / "liveness_triggers.yaml"


def test_triggers_carry_their_own_timeout_and_direction(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # (constructor call, owning instance). The second argument is true for a
    # timeout trigger and false for a recovery one.
    for ctor, instance in (
        ("udp_proxy::MeterLivenessTrigger(120000, true)", "meter_proxy"),
        ("udp_proxy::MeterLivenessTrigger(120000, false)", "meter_proxy"),
        ("mosquitto_broker::DeviceLivenessTrigger(1200000, true)", "local_broker"),
        ("mosquitto_broker::DeviceLivenessTrigger(5400000, true)", "local_broker"),
        ("mosquitto_broker::DeviceLivenessTrigger(1200000, false)", "local_broker"),
        ("marstack::DeviceLivenessTrigger(2700000, true)", "marstack_http"),
        ("marstack::DeviceLivenessTrigger(2700000, false)", "marstack_http"),
    ):
        assert ctor in main_cpp, f"missing {ctor}"
        assert f"{instance}->add_liveness_trigger(" in main_cpp


def test_two_thresholds_on_one_signal_become_two_triggers(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # The fixture's broker declares a 20min note and a 90min action, so each
    # automation must get its own trigger rather than sharing one timeout.
    assert main_cpp.count("local_broker->add_liveness_trigger(") == 3


def test_failsafe_action_reaches_the_local_broker(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # The point of the on-device failsafe: a command goes to the battery
    # through the local broker, with the home broker uninvolved.
    assert "mosquitto_broker::PublishMessageAction" in main_cpp
    assert "marstek_energy/HMJ-2/App/0123456789ab/ctrl" in main_cpp


def test_restart_is_gated_on_the_other_liveness_signal(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # Either side can go quiet on its own, so the documented restart pattern
    # checks the MQTT binary sensor before rebooting on an HTTP timeout.
    assert "restart::RestartSwitch" in main_cpp
    assert "local_broker->set_device_active_binary_sensor(" in main_cpp
    assert "BinarySensorCondition" in main_cpp
