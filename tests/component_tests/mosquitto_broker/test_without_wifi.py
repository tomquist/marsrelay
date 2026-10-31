"""The broker builds without the wifi component (e.g. Ethernet under QEMU).

ESPHome only adds ESP-IDF's esp_wifi to the build when `wifi:` is configured,
so the broker's access point join/leave logging, the one part of it that
needs esp_wifi, must compile out on such a build.
"""
import re
from pathlib import Path

import pytest
from esphome.components import esp32
from esphome.core import CORE

FIXTURES = Path(__file__).parents[3] / "tests" / "fixtures"
SOURCE = (
    Path(__file__).parents[3] / "components" / "mosquitto_broker" / "mosquitto_broker.cpp"
)

# Anything that only exists when esp_wifi is in the build.
_WIFI_ONLY = re.compile(r"esp_wifi\.h|\bWIFI_EVENT|\bWIFI_REASON_|\bwifi_event_")

_KEY_EXCLUDE_COMPONENTS = getattr(esp32, "KEY_EXCLUDE_COMPONENTS", None)


def _defines() -> set[str]:
    return {define.name for define in CORE.defines}


def _esp_wifi_excluded() -> bool:
    return "esp_wifi" in CORE.data[esp32.KEY_ESP32][_KEY_EXCLUDE_COMPONENTS]


@pytest.mark.skipif(
    _KEY_EXCLUDE_COMPONENTS is None,
    reason="ESPHome < 2026.10 always builds esp_wifi",
)
def test_ethernet_only_build_has_no_esp_wifi(generate_main):
    main_cpp = generate_main(str(FIXTURES / "mosquitto_broker_ethernet.yaml"))
    assert "mosquitto_broker::MosquittoBroker" in main_cpp
    assert "USE_WIFI" not in _defines()
    assert _esp_wifi_excluded()


@pytest.mark.skipif(
    _KEY_EXCLUDE_COMPONENTS is None,
    reason="ESPHome < 2026.10 always builds esp_wifi",
)
def test_wifi_build_keeps_the_access_point_logging(generate_main):
    generate_main(str(FIXTURES / "mosquitto_broker_id_mappings.yaml"))
    assert "USE_WIFI" in _defines()
    assert not _esp_wifi_excluded()


def test_esp_wifi_use_is_guarded_by_use_wifi():
    # Tracks the preprocessor conditionals the lines sit in; every esp_wifi
    # reference must be inside an `#ifdef USE_WIFI`.
    stack: list[str] = []
    unguarded = []
    for number, line in enumerate(SOURCE.read_text().splitlines(), start=1):
        directive = line.strip()
        if directive.startswith(("#if", "#ifdef", "#ifndef")):
            stack.append(directive)
        elif directive.startswith("#endif"):
            stack.pop()
        elif directive.startswith("//"):
            continue
        elif _WIFI_ONLY.search(line) and "#ifdef USE_WIFI" not in stack:
            unguarded.append(f"{number}: {directive}")
    assert not stack, "unbalanced preprocessor conditionals"
    assert not unguarded, "esp_wifi used outside #ifdef USE_WIFI:\n" + "\n".join(
        unguarded
    )
