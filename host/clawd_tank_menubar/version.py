# host/clawd_tank_menubar/version.py
"""Version detection for Clawd Tank menu bar app.

At build time (py2app), a _version_info.py file is generated with the
baked-in version string. At dev time, we compute it from git.
"""

import logging
import subprocess

logger = logging.getLogger("clawd-tank.menubar")

_cached_version: str | None = None


def get_version() -> str:
    """Return the version string. Cached after first call."""
    global _cached_version
    if _cached_version is not None:
        return _cached_version

    # 1. Try baked-in version (set during py2app build)
    try:
        from . import _version_info
        _cached_version = _version_info.VERSION
        return _cached_version
    except ImportError:
        pass

    # 2. Compute from git (dev mode)
    _cached_version = _version_from_git()
    return _cached_version


def _version_from_git() -> str:
    """Derive version from git: tag if on one, else branch+sha."""
    try:
        # Check if HEAD is tagged
        tag = subprocess.run(
            ["git", "describe", "--tags", "--exact-match", "HEAD"],
            capture_output=True, text=True, timeout=5,
        )
        if tag.returncode == 0 and tag.stdout.strip():
            return tag.stdout.strip()

        # Not on a tag — use branch + short sha
        branch = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            capture_output=True, text=True, timeout=5,
        )
        sha = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, timeout=5,
        )
        branch_name = branch.stdout.strip() if branch.returncode == 0 else "unknown"
        short_sha = sha.stdout.strip() if sha.returncode == 0 else "??????"
        return f"{branch_name}@{short_sha}"
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return "unknown"
