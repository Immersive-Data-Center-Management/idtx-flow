"""Standardised CMake configure / build / install with consistent error handling.

Replaces the duplicated CMake invocation pattern in ``ixwebsocket.py`` and
the Android branch of ``openusd.py``.
"""
import os
from typing import Sequence, Optional, Mapping

from .run import run_or_exit


def configure(source_dir: str,
              build_dir: str,
              extra_args: Sequence[str],
              description: str,
              env: Optional[Mapping[str, str]] = None) -> None:
    """Run ``cmake -S <source_dir> -B <build_dir> [extra_args ...]``."""
    os.makedirs(build_dir, exist_ok=True)
    cmd = [
        "cmake",
        f"-S{os.path.abspath(source_dir)}",
        f"-B{os.path.abspath(build_dir)}",
        *extra_args,
    ]
    run_or_exit(cmd, f"{description} CMake configure", env=env)


def build(build_dir: str, description: str, config: str = "Release") -> None:
    """Run ``cmake --build <build_dir> --config <config> --parallel``."""
    cmd = ["cmake", "--build", os.path.abspath(build_dir),
           "--config", config, "--parallel"]
    run_or_exit(cmd, f"{description} CMake build")


def install(build_dir: str, description: str, config: str = "Release") -> None:
    """Run ``cmake --install <build_dir> --config <config>``."""
    cmd = ["cmake", "--install", os.path.abspath(build_dir), "--config", config]
    run_or_exit(cmd, f"{description} CMake install")