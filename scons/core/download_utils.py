"""Shared download helpers for SCons tools."""

import hashlib
import tarfile
import urllib.request
import zipfile


def verify_sha256(file_path, expected_sha256):
    print(f"Verifying checksum for {file_path}...")
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as file_handle:
        for byte_block in iter(lambda: file_handle.read(4096), b""):
            sha256_hash.update(byte_block)
    actual = sha256_hash.hexdigest()
    if actual != expected_sha256:
        raise RuntimeError(
            f"SHA256 checksum mismatch!\n  Expected: {expected_sha256}\n  Got:      {actual}"
        )
    print(f"Checksum verified: {actual}")


def download_file(url, dest, label, expected_sha256=None):
    print(f"Downloading {label} from {url}")
    urllib.request.urlretrieve(url, dest)
    if expected_sha256:
        verify_sha256(dest, expected_sha256)


def extract_archive(archive_path, extract_to):
    print(f"Extracting {archive_path} --> {extract_to}")
    if archive_path.endswith(".zip"):
        with zipfile.ZipFile(archive_path, "r") as zip_ref:
            zip_ref.extractall(extract_to)
        return
    if archive_path.endswith(".tar.gz") or archive_path.endswith(".tgz"):
        with tarfile.open(archive_path, "r:gz") as tar_ref:
            tar_ref.extractall(extract_to)
        return
    raise RuntimeError(f"Unsupported archive: {archive_path}")