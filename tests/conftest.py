# SPDX-License-Identifier: MIT OR Apache-2.0
"""Test bootstrap for modules that do not require Home Assistant."""

import sys
from pathlib import Path
from types import ModuleType

# Importing a submodule normally executes the integration's __init__.py, which
# correctly requires Home Assistant. Pure protocol unit tests deliberately run
# without installing the full Home Assistant test environment.
PACKAGE = "custom_components.tentahacle"
package = ModuleType(PACKAGE)
package.__path__ = [
    str(Path(__file__).parents[1] / "custom_components" / "tentahacle")
]
sys.modules[PACKAGE] = package
