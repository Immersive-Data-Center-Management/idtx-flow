"""Locate and capture the MSVC build environment via vswhere + vcvars64.bat.

Extracted from the legacy ``openusd.py`` where it was mis-located. Used when
invoking upstream Python build scripts (e.g. OpenUSD's ``build_usd.py``) on
Windows so they pick up the correct cl.exe/link.exe/include/lib paths.
"""
import os
import subprocess


_VSWHERE_PATH = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"


def get_msvc_env() -> dict:
    """Return ``os.environ`` augmented with the MSVC variables set by vcvars64.bat.

    Raises ``RuntimeError`` if vswhere or a suitable VS installation is missing.
    """
    if not os.path.exists(_VSWHERE_PATH):
        raise RuntimeError("vswhere.exe not found")

    vs_path = subprocess.check_output(
        [_VSWHERE_PATH, "-latest", "-products", "*",
         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"],
        encoding="utf-8",
    ).strip()
    if not vs_path:
        raise RuntimeError("No Visual Studio installation with required components found")

    vcvars = os.path.join(vs_path, "VC", "Auxiliary", "Build", "vcvars64.bat")
    output = subprocess.check_output(f'"{vcvars}" >nul && set', shell=True, text=True)

    env = {}
    for line in output.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            env[key.upper()] = value
    return env