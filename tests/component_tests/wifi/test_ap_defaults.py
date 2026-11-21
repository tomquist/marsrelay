from pathlib import Path
import shutil
import subprocess
import sys

from esphome.config import read_config
from esphome.core import CORE

from tests.component_tests.conftest import generate_main


# Sets wifi.ap without ap_mode or post_connect_roaming, like the shipped config.
FIXTURE = Path(__file__).parents[3] / "tests" / "fixtures" / "udp_proxy.yaml"
COMPONENTS = Path(__file__).parents[3] / "components"
# A broker-only config, which, unlike udp_proxy, needs no station network.
BROKER = Path(__file__).parents[3] / "tests" / "fixtures" / "mosquitto_broker_id_mappings.yaml"
STATION = '  ssid: "test"\n  password: "testpass"\n'


def _broker_config(tmp_path, station=True, ap_extra="", extra=""):
    """The broker fixture, optionally without its station or with AP and top-level extras."""
    text = BROKER.read_text().replace("../../components", COMPONENTS.as_posix())
    assert STATION in text
    if not station:
        text = text.replace(STATION, "")
    text = text.replace('    password: "fallbackpass"\n', '    password: "fallbackpass"\n' + ap_extra, 1)
    path = tmp_path / "config.yaml"
    path.write_text(text + extra)
    return path


def test_marsrelay_keeps_the_ap_up_next_to_the_station(generate_main):
    main_cpp = generate_main(str(FIXTURE))
    # The batteries and meters on the AP need it after the station connects, so
    # every Marsrelay component switches ESPHome's wifi to ap_mode: ALWAYS_ON
    # instead of its default FALLBACK.
    assert "marsrelay_ap" in CORE.loaded_integrations
    assert CORE.config["wifi"]["ap"]["ap_mode"] == "ALWAYS_ON"
    assert any(str(d).startswith("USE_WIFI_AP_PERSISTENT") for d in CORE.defines)
    # ALWAYS_ON requires the roaming scan off, so that is switched off too.
    assert CORE.config["wifi"]["post_connect_roaming"] is False
    assert "set_post_connect_roaming(false)" in main_cpp


def test_wifi_stub_loads_the_builtin_component(generate_main):
    from esphome import loader

    generate_main(str(FIXTURE))
    # components/wifi only stands in for ESPHome's own component, so caches that
    # still hold the old fork's __pycache__ keep working: the module, and the C++
    # sources copied into the build, come from ESPHome itself.
    builtin = loader.CORE_COMPONENTS_PATH / "wifi"
    manifest = loader.get_component("wifi")
    assert Path(manifest.module.__file__) == builtin / "__init__.py"
    sources = {r.resource for r in manifest.resources}
    assert "wifi_component.cpp" in sources
    for resource in manifest.resources:
        with resource.path() as path:
            assert path.parent == builtin


def test_ethernet_counts_as_an_upstream(generate_main, tmp_path):
    # ESPHome accepts ethernet as what ALWAYS_ON keeps the AP up next to
    generate_main(str(_broker_config(tmp_path, station=False, extra="\nethernet:\n  type: OPENETH\n")))
    assert CORE.config["wifi"]["ap"]["ap_mode"] == "ALWAYS_ON"


def test_without_an_upstream_the_ap_stays_a_fallback(generate_main, tmp_path):
    # ALWAYS_ON would be rejected, and the fallback AP is all there is anyway
    generate_main(str(_broker_config(tmp_path, station=False)))
    assert CORE.config["wifi"]["ap"]["ap_mode"] == "FALLBACK"


def test_a_zero_ap_timeout_is_rejected(tmp_path):
    # ALWAYS_ON waits ap_timeout between failed AP starts, so ESPHome refuses 0 for it
    CORE.config_path = _broker_config(tmp_path, ap_extra="    ap_timeout: 0s\n")
    try:
        assert read_config({}) is None
    finally:
        CORE.reset()


def _esphome_config(path):
    return subprocess.run(
        [sys.executable, "-m", "esphome", "config", str(path)],
        capture_output=True,
        text=True,
        check=False,
    )


def test_wifi_stub_shadows_the_old_forks_leftovers(tmp_path):
    # ESPHome's cached copy of this repo keeps the old fork's __pycache__ after an
    # update. Without the stub, that folder loads as an empty wifi component.
    components = tmp_path / "components"
    shutil.copytree(COMPONENTS, components, ignore=shutil.ignore_patterns("__pycache__"))
    stale = components / "wifi" / "__pycache__"
    stale.mkdir()
    (stale / "__init__.cpython-313.pyc").write_bytes(b"")
    config = tmp_path / "config.yaml"
    config.write_text(FIXTURE.read_text().replace("../../components", components.as_posix()))

    result = _esphome_config(config)
    assert result.returncode == 0, result.stdout + result.stderr

    (components / "wifi" / "__init__.py").unlink()
    result = _esphome_config(config)
    assert result.returncode != 0
    assert "no CONFIG_SCHEMA" in result.stdout + result.stderr
