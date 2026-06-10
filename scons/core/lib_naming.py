"""Library file naming helpers.

Centralises the lib-prefix / .so/.dll/.dylib / .a/.lib decisions and the
TBB link-name quirk (``tbb12`` on Windows, ``tbb.12`` on Linux/macOS,
``tbb`` on Android since we cross-build TBB ourselves there).
"""


def shared_lib_filename(info, base_name: str) -> str:
    """Compose a shared library filename from a base name.

    Examples:
        info=Windows, base="idtxflow"  ->  "idtxflow.dll"
        info=Linux,   base="idtxflow"  ->  "libidtxflow.so"
        info=macOS,   base="idtxflow"  ->  "libidtxflow.dylib"
        info=Android, base="idtxflow"  ->  "libidtxflow.so"
    """
    return f"{info.shared_lib_prefix}{base_name}.{info.shared_lib_ext}"


def static_lib_filename(info, base_name: str) -> str:
    """Compose a static library filename from a base name.

    Examples:
        info=Windows-MSVC: base="bootstrap"  ->  "bootstrap.lib"
        info=Linux/macOS:  base="bootstrap"  ->  "libbootstrap.a"
    """
    return f"{info.shared_lib_prefix}{base_name}.{info.static_lib_ext}"


def tbb_link_name(info) -> str:
    """The library name to pass to the linker for OpenUSD's TBB dependency.

    OpenUSD ships TBB differently per platform:
      - Windows: ``tbb12`` (matches ``tbb12.dll`` / ``tbb12.lib``)
      - Linux/macOS: ``tbb.12`` (matches ``libtbb.12.so`` / ``libtbb.12.dylib``)
      - Android: ``tbb`` (we cross-build oneTBB ourselves; produces ``libtbb.so``)
    """
    if info.is_android:
        return "tbb"
    if info.is_windows:
        return "tbb12"
    return "tbb.12"