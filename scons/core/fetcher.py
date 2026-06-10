"""Generic third-party download pipeline.

Replaces the three hand-rolled "download → checksum → extract → rename"
sequences in the legacy ``godotcpp.py``, ``ixwebsocket.py``, ``mdlsdk.py``.

The low-level primitives still come from ``download_utils.py`` at the top
of the ``scons/`` directory.
"""
import os
import sys
from dataclasses import dataclass
from typing import Optional

# ``download_utils`` lives in scons/ (one dir up from scons/core/).
# Make sure it is importable regardless of how scons launches us.
_SCONS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _SCONS_DIR not in sys.path:
    sys.path.insert(0, _SCONS_DIR)

from download_utils import download_file, extract_archive  # noqa: E402


@dataclass(frozen=True)
class ThirdPartySource:
    """Declarative description of a third-party source archive."""
    name: str                       # human-readable label (used in log messages)
    url: str                        # full URL of the archive
    sha256: str                     # expected SHA-256 of the archive
    archive_filename: str           # local filename to use for the archive
    extract_root: str               # directory to extract into
    install_dir: str                # final destination of the extracted source
    extracted_subdir: Optional[str] = None  # name the archive extracts to

    def ensure(self) -> None:
        """No-op if already installed; otherwise download, verify, extract, rename."""
        if os.path.isdir(self.install_dir):
            return

        os.makedirs(self.extract_root, exist_ok=True)
        archive_path = os.path.join(self.extract_root, self.archive_filename)

        download_file(self.url, archive_path, self.name, self.sha256)
        extract_archive(archive_path, self.extract_root)

        if self.extracted_subdir:
            extracted = os.path.join(self.extract_root, self.extracted_subdir)
            if os.path.isdir(extracted) and os.path.abspath(extracted) != os.path.abspath(self.install_dir):
                os.rename(extracted, self.install_dir)

        if os.path.exists(archive_path):
            os.remove(archive_path)