"""Validate metadata required by the Freetz package build."""

import json
import re
from pathlib import Path

PACKAGE_FILES = Path(__file__).parents[1] / "freetz" / "package" / "files"
PACKAGE_MAKEFILE = (
    Path(__file__).parents[1] / "freetz" / "package" / "tentahacle.mk.in"
)
ROOT = Path(__file__).parents[1]


def test_translated_freetz_cgi_is_declared() -> None:
    """Ensure Freetz resolves package labels for fixed-language builds."""
    language_manifest = (PACKAGE_FILES / ".language").read_text()
    cgi_path = "usr/lib/cgi-bin/tentahacle.cgi"

    assert cgi_path in language_manifest
    assert "$(lang" in (PACKAGE_FILES / "root" / cgi_path).read_text()


def test_provider_watchdog_is_packaged() -> None:
    """Keep the local AHA control channel self-healing after deployment."""
    init_script = (
        PACKAGE_FILES / "root" / "etc" / "init.d" / "rc.tentahacle"
    ).read_text()

    assert "provider control channel failed three checks" in init_script
    assert "AHA transport stayed disconnected for twenty minutes" in init_script
    assert '"$0" watchdog' in init_script
    assert "stop_watchdog || return 1" in init_script
    assert "stop_bridge || return 1" in init_script
    assert "tentahacle-mqtt-bridge" in init_script
    assert "MQTT bridge is not running; restarting it" in init_script
    assert "Virtual provider startup failed; watchdog will retry" in init_script
    assert "remaining=40" in init_script
    assert "LOG_MAX_BYTES=1048576" in init_script


def test_bridge_reports_the_resolved_package_version() -> None:
    """Do not defer the package-name variable beyond Freetz's include phase."""
    makefile = PACKAGE_MAKEFILE.read_text()

    assert '-DTVB_VERSION=\\"$(TENTAHACLE_VERSION)\\"' in makefile
    assert '-DTVB_VERSION=\\"$($(PKG)_VERSION)\\"' not in makefile


def test_home_assistant_freetz_and_changelog_versions_match() -> None:
    """Keep the two installable halves on one release version."""
    version = json.loads(
        (ROOT / "custom_components/tentahacle/manifest.json").read_text()
    )["version"]
    makefile = PACKAGE_MAKEFILE.read_text()
    match = re.search(r"^\$\(call PKG_INIT_BIN, ([^)]+)\)$", makefile, re.MULTILINE)
    assert match is not None
    assert match.group(1) == version
    assert f"## {version} -" in (ROOT / "CHANGELOG.md").read_text()


def test_native_and_home_assistant_capacity_match() -> None:
    """Keep the independently installed halves on one device ceiling."""
    native_header = (ROOT / "freetz/package/src/device_registry.h").read_text()
    ha_constants = (ROOT / "custom_components/tentahacle/const.py").read_text()
    native = re.search(
        r"^#define TVB_REGISTRY_CAPACITY (\d+)u$", native_header, re.MULTILINE
    )
    home_assistant = re.search(
        r"^MAX_VIRTUAL_DEVICES = (\d+)$", ha_constants, re.MULTILINE
    )
    assert native is not None
    assert home_assistant is not None
    assert native.group(1) == home_assistant.group(1) == "64"


def test_package_version_is_stripped_by_freetz_modload() -> None:
    """Keep prerelease syntax compatible with Freetz's package-name parser."""
    makefile = PACKAGE_MAKEFILE.read_text()
    match = re.search(r"^\$\(call PKG_INIT_BIN, ([^)]+)\)$", makefile, re.MULTILINE)
    assert match is not None

    package_directory = f"tentahacle-{match.group(1)}"
    # Mirror tools/freetz_functions:real_pkg_name(). If the version is not
    # stripped, /etc/static.pkg contains the versioned directory and modload
    # cannot find /etc/default.tentahacle or rc.tentahacle at boot.
    package_name = re.sub(
        r"-(alpha|beta|rc|svn|(un)?stable|.)[0-9]*$",
        "",
        package_directory,
        flags=re.IGNORECASE,
    )
    package_name = re.sub(
        r"-[0-9a-f]{7}([0-9a-f]([0-9a-f]{2}([0-9a-f]{30})?)?)?$",
        "",
        package_name,
    )
    package_name = re.sub(r"-v?[0-9][^-]*$", "", package_name)
    package_name = re.sub(r"-v?[0-9][^-]*$", "", package_name)

    assert package_name == "tentahacle"
