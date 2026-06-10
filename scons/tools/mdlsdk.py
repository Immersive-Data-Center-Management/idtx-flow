"""SCons tool: mdlsdk (refactored)

Downloads and extracts NVIDIA's MDL SDK. Skipped on Android — MDL SDK
isn't shipped for that platform.

Registers the SCons env method ``DownloadMdlSdkV2``.
"""
import os
import platform
import shutil
import sys

from scons.core.fetcher import ThirdPartySource


MDL_VERSION        = "2025.0.0-387700.1252"
MDL_RELEASE_FOLDER = "2025"
BASE_URL           = "https://github.com/NVIDIA/MDL-SDK/releases/download"

MDL_CHECKSUMS = {
    "windows-x86-64":  "407464bb19371ad3dc92fb64db52af6ece2177a48d6811dc0f461de3f392b546",
    "linux-x86-64":    "943a035bb08a4dce282a0f925ea2a0bd45a0bdcea3a4988c9e30c12ed316c5f4",
    "linux-aarch64":   "a1b1574ef0188787bdc3df56dd0508a724477e747acc72b55c2a17503333ac9d",
    "macosx-x86-64":   "28d6b4d9f47944d8b4c75fac42f22fb19123d1db7d108185dbdaa1bf95ce2c05",
    "macosx-aarch64":  "d641fb96e4e02f090e9c597725873ada524de65e447c3043c99705dd3cbc5987",
}

_ARCH_MAP = {
    "aarch64": "aarch64",
    "arm64":   "aarch64",
    "x86_64":  "x86-64",
    "amd64":   "x86-64",
    "x64":     "x86-64",
}


def generate(env):
    env.AddMethod(_download_mdl_sdk, "DownloadMdlSdkV2")


def exists(env):
    return True


def _download_mdl_sdk(env):
    profile      = env["profile"]
    target       = profile.platform
    paths        = profile.paths

    if target.is_android:
        print("Skipping MDL SDK download (not available for Android).")
        return

    if os.path.isdir(paths.mdl_sdk):
        print("MDL SDK already present.")
        return

    host_machine = _ARCH_MAP.get(platform.machine().lower(), platform.machine().lower())

    if target.is_windows:
        filename = f"MDL-SDK-{MDL_VERSION}-nt-{host_machine}.zip"
        checksum_key = f"windows-{host_machine}"
    elif target.is_linux:
        filename = f"mdl-sdk-{MDL_VERSION}-linux-{host_machine}.tgz"
        checksum_key = f"linux-{host_machine}"
    elif target.is_macos:
        filename = f"MDL-SDK-{MDL_VERSION}-macosx-{host_machine}.tgz"
        checksum_key = f"macosx-{host_machine}"
    else:
        print(f"Unsupported platform for MDL SDK auto-download: {target.name}")
        sys.exit(1)

    expected_checksum = MDL_CHECKSUMS.get(checksum_key)
    if not expected_checksum:
        print(f"No checksum available for platform {checksum_key}")
        sys.exit(1)

    src = ThirdPartySource(
        name="MDL SDK",
        url=f"{BASE_URL}/{MDL_RELEASE_FOLDER}/{filename}",
        sha256=expected_checksum,
        archive_filename=filename,
        extract_root=paths.thirdparty,
        # The archive doesn't extract to a stable subdir name, so we let
        # ThirdPartySource just download+extract; we then move below.
        install_dir=paths.mdl_sdk,
        extracted_subdir=filename.rsplit('.', 1)[0],
    )
    src.ensure()

    # ThirdPartySource handles the rename for us when ``extracted_subdir``
    # matches; if for any reason the extracted directory is still present
    # under its original name, move it into place.
    legacy_extracted = os.path.join(paths.thirdparty, filename.rsplit('.', 1)[0])
    if os.path.isdir(legacy_extracted) and not os.path.isdir(paths.mdl_sdk):
        shutil.move(legacy_extracted, paths.mdl_sdk)

    print("MDL SDK installed successfully.")