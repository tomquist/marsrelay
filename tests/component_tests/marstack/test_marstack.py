from pathlib import Path

import pytest
from esphome.config import read_config
from esphome.core import CORE


REPO_ROOT = Path(__file__).parents[3]
FIXTURE = REPO_ROOT / "tests" / "fixtures" / "marstack_https.yaml"


def _derive(tmp_path: Path, *replacements: tuple[str, str]) -> Path:
    """Copy the fixture into tmp_path with `replacements` applied."""
    config = FIXTURE.read_text().replace(
        "path: ../../components", f"path: {REPO_ROOT / 'components'}"
    )
    for old, new in replacements:
        assert old in config, old
        config = config.replace(old, new)
    path = tmp_path / "config.yaml"
    path.write_text(config)
    return path


def _read(path: Path):
    CORE.config_path = path
    try:
        return read_config({})
    finally:
        CORE.reset()


def test_marstack_https_generation(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    assert "marstack::Marstack" in main_cpp
    assert "marstack::MarstackHttps" in main_cpp
    assert "set_port(443)" in main_cpp
    assert "set_accept_all(false)" in main_cpp
    assert "set_hold_time(25000)" in main_cpp
    assert "set_max_body(4096)" in main_cpp
    assert "set_max_connections(2)" in main_cpp
    # Byte-exact cloud framing is on unless a config turns it off.
    assert "set_raw_responses(true)" in main_cpp
    assert 'set_time_suffix("04_0_0_0")' in main_cpp
    assert "add_venus_upload_trigger(" in main_cpp


def test_marstack_without_https_generates_no_listener(generate_main, tmp_path):
    config = FIXTURE.read_text()
    https_block = config[config.index("  https:\n") : config.index("  on_request:\n")]
    path = _derive(tmp_path, (https_block, ""))

    main_cpp = generate_main(str(path))
    assert "marstack::Marstack" in main_cpp
    assert "MarstackHttps" not in main_cpp


@pytest.mark.parametrize("hold_time", ["1s", "20s"])
def test_hold_time_shorter_than_firmware_timeout_is_rejected(hold_time, tmp_path):
    # Closing while the battery is still reading makes it discard the reply and
    # keep the telemetry record buffered, which is the failure this component
    # exists to avoid.
    path = _derive(tmp_path, ("hold_time: 25s", f"hold_time: {hold_time}"))
    assert _read(path) is None


def test_hold_time_zero_is_allowed(tmp_path):
    path = _derive(tmp_path, ("hold_time: 25s", "hold_time: 0s"))
    assert _read(path) is not None


def test_https_port_clashing_with_web_server_is_rejected(tmp_path):
    path = _derive(tmp_path, ("port: 8080", "port: 443"))
    assert _read(path) is None
