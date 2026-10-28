from pathlib import Path

import pytest
from esphome.config import read_config
from esphome.core import CORE


REPO_ROOT = Path(__file__).parents[3]
FIXTURE = REPO_ROOT / "tests" / "fixtures" / "mosquitto_broker_id_mappings.yaml"


def _with_recovery(tmp_path: Path, recovery: str) -> Path:
    config = FIXTURE.read_text().replace(
        "path: ../../components", f"path: {REPO_ROOT / 'components'}"
    )
    anchor = "  port: 1883\n"
    assert anchor in config
    config = config.replace(anchor, f"{anchor}  recover_stuck_devices:\n{recovery}")
    path = tmp_path / "config.yaml"
    path.write_text(config)
    return path


def _read(path: Path):
    CORE.config_path = path
    try:
        return read_config({})
    finally:
        CORE.reset()


def test_recovery_is_off_by_default(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    assert "set_stuck_recovery(" not in main_cpp


WRAP = "-Wl,--wrap=hostap_new_assoc_sta"
KEEP = "-Wl,--undefined=__wrap_hostap_new_assoc_sta"


def test_recovery_defaults(generate_main, tmp_path):
    # An empty block turns it on with the defaults: 5 min unanswered, 60 s off.
    main_cpp = generate_main(str(_with_recovery(tmp_path, "    {}\n")))
    assert "local_broker->set_stuck_recovery(300000, 60000)" in main_cpp


def test_recovery_adds_the_association_wrap(generate_main, tmp_path):
    generate_main(str(_with_recovery(tmp_path, "    {}\n")))
    assert WRAP in CORE.build_flags
    assert KEEP in CORE.build_flags


def test_no_wrap_without_recovery(generate_main):
    generate_main(str(FIXTURE))
    assert WRAP not in CORE.build_flags
    assert KEEP not in CORE.build_flags


def test_recovery_settings_reach_the_component(generate_main, tmp_path):
    path = _with_recovery(tmp_path, "    timeout: 10min\n    duration: 45s\n")
    main_cpp = generate_main(str(path))
    assert "local_broker->set_stuck_recovery(600000, 45000)" in main_cpp


@pytest.mark.parametrize(
    "recovery",
    [
        "    timeout: 30s\n",  # shorter than a polling interval or two
        "    duration: 20s\n",  # too short for the battery to miss the access point
        "    duration: 11min\n",
    ],
)
def test_out_of_range_settings_are_rejected(recovery, tmp_path):
    assert _read(_with_recovery(tmp_path, recovery)) is None
