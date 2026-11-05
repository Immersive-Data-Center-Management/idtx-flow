"""vcpkg bootstrap and package install helpers.

Extracted from the legacy ``ixwebsocket.py``. Used both by IXWebSocket
(at build time) and by the GDExtension link step (to find OpenSSL on
Windows / when system OpenSSL is not available on Linux/macOS).
"""
import os
import platform

from .run import run_or_exit


VCPKG_REPO = "https://github.com/microsoft/vcpkg.git"


def vcpkg_root_default() -> str:
    """Default location for the cloned vcpkg checkout."""
    return os.path.abspath("thirdparty/vcpkg")


def vcpkg_executable(vcpkg_root: str) -> str:
    return os.path.join(vcpkg_root,
                        "vcpkg.exe" if platform.system() == "Windows" else "vcpkg")


def ensure_vcpkg(vcpkg_root: str = None) -> str:
    """Clone and bootstrap vcpkg if not already present. Returns the root path."""
    if vcpkg_root is None:
        vcpkg_root = vcpkg_root_default()

    if os.path.exists(vcpkg_executable(vcpkg_root)):
        return vcpkg_root

    if not os.path.exists(vcpkg_root):
        print("Cloning vcpkg...")
        run_or_exit(
            ["git", "clone", "--depth", "1", VCPKG_REPO, vcpkg_root],
            "vcpkg clone",
        )

    print("Bootstrapping vcpkg...")
    if platform.system() == "Windows":
        bootstrap = os.path.join(vcpkg_root, "bootstrap-vcpkg.bat")
        run_or_exit([bootstrap, "-disableMetrics"],
                    "vcpkg bootstrap", cwd=vcpkg_root, shell=True)
    else:
        bootstrap = os.path.join(vcpkg_root, "bootstrap-vcpkg.sh")
        run_or_exit(["bash", bootstrap, "-disableMetrics"],
                    "vcpkg bootstrap", cwd=vcpkg_root)

    return vcpkg_root


def install_package(vcpkg_root: str, package: str, triplet: str) -> None:
    """Install ``<package>:<triplet>`` via vcpkg if not already installed.

    The "already installed" probe is left to the caller — vcpkg itself is
    fast at deciding it doesn't need to do anything, but probing avoids
    network calls in the common case.
    """
    print(f"Installing {package} via vcpkg ({triplet})...")
    run_or_exit(
        [vcpkg_executable(vcpkg_root), "install", f"{package}:{triplet}", "--recurse"],
        f"vcpkg install {package}",
        cwd=vcpkg_root,
    )


def triplet_for(info) -> str:
    """Pick the correct vcpkg triplet for a given PlatformInfo.

    Static triplets are preferred so we don't drag DLLs into our build output.
    """
    if info.is_windows:
        return "x64-windows-static"
    if info.is_macos:
        return "arm64-osx" if info.arch == "arm64" else "x64-osx"
    if info.is_linux:
        return "arm64-linux" if info.arch == "arm64" else "x64-linux"
    raise ValueError(f"vcpkg is not supported for platform '{info.name}'")


def installed_lib_dir(vcpkg_root: str, triplet: str) -> str:
    return os.path.join(vcpkg_root, "installed", triplet, "lib")


def installed_include_dir(vcpkg_root: str, triplet: str) -> str:
    return os.path.join(vcpkg_root, "installed", triplet, "include")


def toolchain_file(vcpkg_root: str) -> str:
    return os.path.join(vcpkg_root, "scripts", "buildsystems", "vcpkg.cmake")