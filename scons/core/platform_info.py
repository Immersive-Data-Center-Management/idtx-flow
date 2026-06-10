"""PlatformInfo — single source of truth for the build target platform.

Created once in SConstruct (or SConstruct.new) and stashed on
``env["platform_info"]``. Every tool reads from it instead of poking at
``platform.system()`` directly.
"""
from dataclasses import dataclass
import platform


@dataclass(frozen=True)
class PlatformInfo:
    name: str            # "windows" | "linux" | "macos" | "android"
    arch: str            # "x86_64" | "arm64"
    target: str          # "template_debug" | "template_release" | "editor" | "debug" | "release"
    is_msvc: bool        # True only when CXX=cl (i.e. desktop Windows MSVC)

    # Convenience predicates
    @property
    def is_windows(self) -> bool: return self.name == "windows"
    @property
    def is_linux(self) -> bool: return self.name == "linux"
    @property
    def is_macos(self) -> bool: return self.name == "macos"
    @property
    def is_android(self) -> bool: return self.name == "android"
    @property
    def is_desktop(self) -> bool: return self.name in ("windows", "linux", "macos")

    # Library naming
    @property
    def shared_lib_ext(self) -> str:
        return {"windows": "dll", "macos": "dylib",
                "linux": "so", "android": "so"}[self.name]

    @property
    def static_lib_ext(self) -> str:
        return "lib" if self.is_msvc else "a"

    @property
    def shared_lib_prefix(self) -> str:
        return "" if self.is_msvc else "lib"

    @property
    def exe_ext(self) -> str:
        return ".exe" if self.is_windows else ""

    # Detection
    @classmethod
    def detect(cls, args) -> "PlatformInfo":
        """Construct a PlatformInfo from SCons ARGUMENTS (a dict-like)."""
        target_platform = args.get("platform")
        if target_platform is None:
            sys_name = platform.system()
            if sys_name == "Windows":
                target_platform = "windows"
            elif sys_name == "Darwin":
                target_platform = "macos"
            else:
                target_platform = "linux"

        if target_platform == "android":
            arch = args.get("arch", "arm64")
        else:
            host_arch = platform.machine().lower()
            if host_arch in ("aarch64", "arm64"):
                host_arch = "arm64"
            elif host_arch in ("x86_64", "amd64", "x64"):
                host_arch = "x86_64"
            arch = args.get("arch", host_arch)

        return cls(
            name=target_platform,
            arch=arch,
            target=args.get("target", "template_debug"),
            is_msvc=(target_platform == "windows"),
        )