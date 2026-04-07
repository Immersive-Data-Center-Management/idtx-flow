"""
SCons tool: mdlsdk
Downloads and extracts the NVIDIA MDL SDK for the current platform.
The MDL SDK is a dependency for the IDTXFlow GDExtension.

Usage in SConstruct:
    env.DownloadMdlSdk()
"""
import os
import sys
import platform
import urllib.request
import zipfile
import tarfile
import shutil
from SCons.Script import Exit

# Configure MDL version here:
MDL_VERSION = "2025.0.0-387700.1252"
MDL_RELEASE_FOLDER = "2025"
BASE_URL = "https://github.com/NVIDIA/MDL-SDK/releases/download"

def generate(env):
    env.AddMethod(_downloadMdlSdk, "DownloadMdlSdk")

def exists(env):
    return True

def _download_file(url, dest):
    print(f"Downloading MDL SDK from {url}")
    urllib.request.urlretrieve(url, dest)

def _extract_archive(archive_path, extract_to):
    print(f"Extracting {archive_path} --> {extract_to}")

    if archive_path.endswith(".zip"):
        with zipfile.ZipFile(archive_path, "r") as zip_ref:
            zip_ref.extractall(extract_to)
    elif archive_path.endswith(".tar.gz") or archive_path.endswith(".tgz"):
        with tarfile.open(archive_path, "r:gz") as tar_ref:
            tar_ref.extractall(extract_to)
    else:
        raise RuntimeError(f"Unsupported archive: {archive_path}")

def _downloadMdlSdk(env):
    # MDL SDK is not available for Android — skip entirely
    if env.get('is_android', False):
        print("Skipping MDL SDK download (not available for Android).")
        return

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
    elif platform_id == "linux":
        filename = f"mdl-sdk-{MDL_VERSION}-linux-{machine}.tgz"
    elif platform_id == "darwin":
        filename = f"MDL-SDK-{MDL_VERSION}-macosx-{machine}.tgz"
    else:
        print("Unsupported platform for MDL SDK auto-download.")
        sys.exit(1)

    url = f"{BASE_URL}/{MDL_RELEASE_FOLDER}/{filename}"
    archive_path = os.path.join("./thirdparty", filename)

    _download_file(url, archive_path)

    _extract_archive(archive_path, "./thirdparty")

    # The archive contains mdl-sdk-${VERSION}/... -> rename to mdl_sdk
    extracted_dir = os.path.join(
        "./thirdparty", filename.rsplit('.', 1)[0]  # Remove .zip or .tgz
    )
    shutil.move(extracted_dir, mdl_root)

    print("MDL SDK installed successfully.")