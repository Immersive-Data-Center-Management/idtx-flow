"""subprocess.run wrapper that exits SCons cleanly on failure.

Replaces the boilerplate ``if result.returncode != 0: Exit(...)`` pattern
duplicated across many tools.
"""
import subprocess
from typing import Sequence, Optional, Mapping

from SCons.Script import Exit


def run_or_exit(cmd: Sequence[str],
                description: str,
                cwd: Optional[str] = None,
                env: Optional[Mapping[str, str]] = None,
                shell: bool = False) -> None:
    """Run ``cmd`` and call ``Exit(...)`` on non-zero exit code."""
    result = subprocess.run(cmd, cwd=cwd, env=env, shell=shell)
    if result.returncode != 0:
        Exit(f"{description} failed (exit code: {result.returncode})")