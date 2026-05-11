"""
SCons tool: mdlsdk
Downloads and extracts the NVIDIA MDL SDK for the current platform.
The MDL SDK is a dependency for the IDTXFlow GDExtension.

Usage in SConstruct:
    env.DownloadMdlSdk()
"""
import os
import platform
import shutil
import sys

from download_utils import download_file, extract_archive

# Configure MDL version here:
MDL_VERSION = "2025.0.0-387700.1252"
MDL_RELEASE_FOLDER = "2025"
BASE_URL = "https://github.com/NVIDIA/MDL-SDK/releases/download"

# SHA256 checksums for each platform/architecture combination
MDL_CHECKSUMS = {
    "windows-x86-64": "407464bb19371ad3dc92fb64db52af6ece2177a48d6811dc0f461de3f392b546",
    "linux-x86-64": "943a035bb08a4dce282a0f925ea2a0bd45a0bdcea3a4988c9e30c12ed316c5f4",
    "linux-aarch64": "a1b1574ef0188787bdc3df56dd0508a724477e747acc72b55c2a17503333ac9d",
    "macosx-x86-64": "28d6b4d9f47944d8b4c75fac42f22fb19123d1db7d108185dbdaa1bf95ce2c05",
    "macosx-aarch64": "d641fb96e4e02f090e9c597725873ada524de65e447c3043c99705dd3cbc5987",
}

def generate(env):
    env.AddMethod(_downloadMdlSdk, "DownloadMdlSdk")

def exists(env):
    return True

def _downloadMdlSdk(env):
    platform_id = platform.system().lower()
    shared_deps_root = env.get('SHARED_DEPS_ROOT', '')
    mdl_root = os.path.join("./thirdparty", "mdl_sdk")

    if os.path.isdir(mdl_root):
        print("MDL SDK already present.")
        return

    os.makedirs("./thirdparty", exist_ok=True)

    machine = platform.machine().lower()
    arch_map = {
        'aarch64': 'aarch64',
        'arm64': 'aarch64',
        'x86_64': 'x86-64',
        'amd64': 'x86-64',
        'x64': 'x86-64'
    }

    machine = arch_map.get(machine, machine)

    if platform_id == "windows":
        filename = f"MDL-SDK-{MDL_VERSION}-nt-{machine}.zip"
        checksum_key = f"windows-{machine}"
    elif platform_id == "linux":
        filename = f"mdl-sdk-{MDL_VERSION}-linux-{machine}.tgz"
        checksum_key = f"linux-{machine}"
    elif platform_id == "darwin":
        filename = f"MDL-SDK-{MDL_VERSION}-macosx-{machine}.tgz"
        checksum_key = f"macosx-{machine}"
    else:
        print("Unsupported platform for MDL SDK auto-download.")
        sys.exit(1)

    expected_checksum = MDL_CHECKSUMS.get(checksum_key)
    if not expected_checksum:
        print(f"No checksum available for platform {checksum_key}")
        sys.exit(1)

    url = f"{BASE_URL}/{MDL_RELEASE_FOLDER}/{filename}"
    archive_path = os.path.join("./thirdparty", filename)

    download_file(url, archive_path, "MDL SDK", expected_checksum)

    extract_archive(archive_path, "./thirdparty")

    # The archive contains mdl-sdk-${VERSION}/... -> rename to mdl_sdk
    extracted_dir = os.path.join(
        "./thirdparty", filename.rsplit('.', 1)[0]  # Remove .zip or .tgz
    )
    shutil.move(extracted_dir, mdl_root)

    print("MDL SDK installed successfully.")