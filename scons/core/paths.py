"""BuildPaths — centralised path constants.

All hard-coded ``thirdparty/...``, ``build/...``, ``addons/...`` paths live
here. Renaming a folder is a one-line change.
"""
from dataclasses import dataclass

from .platform_info import PlatformInfo


@dataclass(frozen=True)
class BuildPaths:
    platform: PlatformInfo
    openusd_version: str

    # ---- Third-party roots --------------------------------------------------
    @property
    def thirdparty(self) -> str:
        return "thirdparty"

    @property
    def godot_cpp(self) -> str:
        return f"{self.thirdparty}/godot-cpp"

    @property
    def ixwebsocket(self) -> str:
        return f"{self.thirdparty}/ixwebsocket"

    @property
    def ixwebsocket_build(self) -> str:
        return f"{self.ixwebsocket}/build_{self.platform.name}_{self.platform.target}"

    @property
    def mdl_sdk(self) -> str:
        return f"{self.thirdparty}/mdl_sdk"

    @property
    def vcpkg(self) -> str:
        return f"{self.thirdparty}/vcpkg"

    @property
    def openusd_src(self) -> str:
        return f"{self.thirdparty}/openusd-{self.openusd_version}-src"

    @property
    def openusd_install(self) -> str:
        if self.platform.is_android:
            return f"{self.thirdparty}/openusd-{self.openusd_version}-android"
        return f"{self.thirdparty}/openusd-{self.openusd_version}"

    @property
    def openusd_python(self) -> str:
        return f"{self.thirdparty}/openusd-{self.openusd_version}-withPython"

    @property
    def onetbb_android(self) -> str:
        return f"{self.thirdparty}/onetbb-android"

    # ---- IDTX build / install / SDK output ---------------------------------
    @property
    def gdext_build(self) -> str:
        return f"build/IDTXFlow/bin/{self.platform.name}"

    @property
    def addon_install(self) -> str:
        return f"addons/IDTXFlow/bin/{self.platform.name}"

    @property
    def addon_plugin_install(self) -> str:
        return "addons/IDTXFlow/bin/plugin/usd"

    @property
    def sdk_root(self) -> str:
        return "build/idtxflow-sdk"

    @property
    def sdk_includes(self) -> str:
        return f"{self.sdk_root}/include"

    @property
    def sdk_libs(self) -> str:
        return f"{self.sdk_root}/lib"

    @property
    def usd_ext_root(self) -> str:
        return "usd"

    @property
    def usd_ext_build(self) -> str:
        return f"{self.usd_ext_root}/build/{self.platform.name}"

    @property
    def usd_ext_libs(self) -> str:
        return f"{self.usd_ext_root}/libs/{self.platform.name}"

    @property
    def usd_ext_include(self) -> str:
        return f"{self.usd_ext_root}/include"

    @property
    def usd_ext_generated(self) -> str:
        return f"{self.usd_ext_root}/generated"

    @property
    def shared_include(self) -> str:
        return "shared/include"

    @property
    def shared_libs(self) -> str:
        return "shared/libs"

    @property
    def ext_bootstrap_build(self) -> str:
        return "build/idtxflow_ext_bootstrap"