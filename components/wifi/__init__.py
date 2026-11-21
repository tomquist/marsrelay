"""Hands over to ESPHome's own wifi component.

Marsrelay used to ship a fork of the wifi component in this folder. ESPHome
updates its cached copy of this repository without deleting untracked files,
so the fork's __pycache__ folder stays behind there. With no component left in
this folder, ESPHome would load that leftover as an empty wifi component and
fail with "Component wifi cannot be loaded via YAML (no CONFIG_SCHEMA)".

This file keeps a real component here instead: it loads ESPHome's built-in
wifi component, and its C++ sources, in place of itself. Keep it for as long
as configs that once built the fork may still be updated.
"""

import importlib.util
import sys

from esphome.loader import CORE_COMPONENTS_PATH

_BUILTIN = CORE_COMPONENTS_PATH / "wifi"

_spec = importlib.util.spec_from_file_location(
    __name__, _BUILTIN / "__init__.py", submodule_search_locations=[str(_BUILTIN)]
)
_module = importlib.util.module_from_spec(_spec)
# The import system returns whatever sys.modules holds once this file has run
sys.modules[__name__] = _module
_spec.loader.exec_module(_module)
