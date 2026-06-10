"""The mechanical applicator — spreads a profile section into env.Append.

A profile section is just a ``Mapping[str, list]`` (e.g. ``{"CXXFLAGS":
["/EHsc", "/std:c++20"], "CCFLAGS": ["/O2", "/MT"]}``). Applying it does
nothing more than call ``env.Append(**section)``. This single helper
exists so every env mutation in tools is one line, easy to grep for, and
free of decisions.

Usage:
    from scons.core.apply import apply_profile_section, apply_profile_sections

    apply_profile_section(env, profile.cxx_flags)
    apply_profile_sections(env, [profile.cxx_flags, profile.shared_lib_flags])
"""
from typing import Mapping, Iterable


def apply_profile_section(env, section: Mapping[str, list]) -> None:
    """Apply one profile section to ``env`` via a single ``env.Append``.

    Empty sections are a no-op."""
    if not section:
        return
    env.Append(**section)


def apply_profile_sections(env, sections: Iterable[Mapping[str, list]]) -> None:
    """Apply multiple profile sections in order. Convenience wrapper."""
    for section in sections:
        apply_profile_section(env, section)