"""Single source of truth for OpenSSL discovery.

Replaces the duplicated probes in the legacy ``ixwebsocket.py`` and
``gdextension.py`` (which had subtly different logic and could drift).

Strategy on each platform:
  - Windows  : always vcpkg (no system OpenSSL).
  - macOS    : Homebrew (with @3 variants) → pkg-config → vcpkg.
  - Linux    : standard distro paths → pkg-config → vcpkg.
  - Android  : not supported here; the IXWebSocket build uses USE_TLS=OFF
               on Android. Caller must check ``info.is_android`` first.

Returns a dataclass so callers can pick the bits they need (``include_dir``,
``lib_dir``, ``cmake_root_dir``, ``source`` for diagnostics).
"""
import os
import platform
import subprocess
from dataclasses import dataclass
from typing import Optional

from . import vcpkg


@dataclass(frozen=True)
class OpenSSLLocation:
    include_dir: str
    lib_dir: str
    source: str                # "system" | "homebrew" | "pkgconfig" | "vcpkg"
    vcpkg_root: Optional[str] = None
    vcpkg_triplet: Optional[str] = None

    @property
    def cmake_root_dir(self) -> str:
        """Best path to pass to CMake's ``-DOPENSSL_ROOT_DIR=``."""
        # Both include_dir and lib_dir live under <prefix>/include and <prefix>/lib
        return os.path.dirname(self.include_dir)

    @property
    def is_vcpkg(self) -> bool:
        return self.source == "vcpkg"


def find_openssl(info) -> Optional[OpenSSLLocation]:
    """Return an OpenSSLLocation, installing via vcpkg as a last resort.

    Returns ``None`` only on Android (where TLS is disabled).
    """
    if info.is_android:
        return None

    if info.is_macos:
        for prefix in ("/opt/homebrew/opt/openssl",
                       "/usr/local/opt/openssl",
                       "/opt/homebrew/opt/openssl@3",
                       "/usr/local/opt/openssl@3"):
            inc = os.path.join(prefix, "include")
            lib = os.path.join(prefix, "lib")
            if os.path.isfile(os.path.join(inc, "openssl", "ssl.h")) and os.path.isdir(lib):
                return OpenSSLLocation(include_dir=inc, lib_dir=lib, source="homebrew")

    if info.is_linux:
        for inc, lib in (("/usr/include", "/usr/lib/x86_64-linux-gnu"),
                         ("/usr/include", "/usr/lib64"),
                         ("/usr/include", "/usr/lib"),
                         ("/usr/local/include", "/usr/local/lib")):
            if os.path.isfile(os.path.join(inc, "openssl", "ssl.h")) and os.path.isdir(lib):
                return OpenSSLLocation(include_dir=inc, lib_dir=lib, source="system")

    if not info.is_windows:
        pc = _probe_pkg_config()
        if pc:
            inc, lib = pc
            return OpenSSLLocation(include_dir=inc, lib_dir=lib, source="pkgconfig")

    # Last resort (and the only option on Windows): vcpkg
    return _install_via_vcpkg(info)


def _probe_pkg_config() -> Optional[tuple]:
    try:
        inc = subprocess.check_output(
            ["pkg-config", "--variable=includedir", "openssl"],
            stderr=subprocess.DEVNULL, text=True,
        ).strip()
        lib = subprocess.check_output(
            ["pkg-config", "--variable=libdir", "openssl"],
            stderr=subprocess.DEVNULL, text=True,
        ).strip()
        if inc and lib and os.path.isfile(os.path.join(inc, "openssl", "ssl.h")):
            return (inc, lib)
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass
    return None


def _install_via_vcpkg(info) -> OpenSSLLocation:
    root = vcpkg.ensure_vcpkg()
    triplet = vcpkg.triplet_for(info)

    lib_dir = vcpkg.installed_lib_dir(root, triplet)
    inc_dir = vcpkg.installed_include_dir(root, triplet)

    # Probe whether OpenSSL is already present (don't shell out to vcpkg
    # every build).
    if platform.system() == "Windows":
        marker = os.path.join(lib_dir, "libssl.lib")
    else:
        marker = os.path.join(lib_dir, "libssl.a")

    if not os.path.exists(marker):
        vcpkg.install_package(root, "openssl", triplet)

    return OpenSSLLocation(
        include_dir=inc_dir,
        lib_dir=lib_dir,
        source="vcpkg",
        vcpkg_root=root,
        vcpkg_triplet=triplet,
    )


def link_libnames(info) -> list:
    """Return the ``-l...`` / ``...lib`` names to pass to the linker.

    On Windows (vcpkg static OpenSSL) the names are ``libssl`` / ``libcrypto``;
    elsewhere they're ``ssl`` / ``crypto``.
    """
    if info.is_windows:
        return ["libssl", "libcrypto"]
    return ["ssl", "crypto"]