from pathlib import Path

from esphome.core import CORE

from tests.component_tests.conftest import generate_main


# Sets wifi.ap without ap_mode or post_connect_roaming, like the shipped config.
FIXTURE = Path(__file__).parents[3] / "tests" / "fixtures" / "udp_proxy.yaml"


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
