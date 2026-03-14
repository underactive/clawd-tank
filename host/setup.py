# host/setup.py — py2app configuration for Clawd Tank menubar app
import subprocess
from pathlib import Path
from setuptools import setup


def _bake_version():
    """Generate _version_info.py with the current git version."""
    try:
        tag = subprocess.run(
            ["git", "describe", "--tags", "--exact-match", "HEAD"],
            capture_output=True, text=True, timeout=5,
        )
        if tag.returncode == 0 and tag.stdout.strip():
            version = tag.stdout.strip()
        else:
            branch = subprocess.run(
                ["git", "rev-parse", "--abbrev-ref", "HEAD"],
                capture_output=True, text=True, timeout=5,
            )
            sha = subprocess.run(
                ["git", "rev-parse", "--short", "HEAD"],
                capture_output=True, text=True, timeout=5,
            )
            b = branch.stdout.strip() if branch.returncode == 0 else "unknown"
            s = sha.stdout.strip() if sha.returncode == 0 else "??????"
            version = f"{b}@{s}"
    except (FileNotFoundError, subprocess.TimeoutExpired):
        version = "unknown"

    path = Path(__file__).parent / "clawd_tank_menubar" / "_version_info.py"
    path.write_text(f'VERSION = "{version}"\n')
    print(f"Baked version: {version}")


_bake_version()

APP = ["launcher.py"]
DATA_FILES = []
OPTIONS = {
    "argv_emulation": False,
    "iconfile": "AppIcon.icns",
    "plist": {
        "CFBundleName": "Clawd Tank",
        "CFBundleDisplayName": "Clawd Tank",
        "CFBundleIdentifier": "com.clawd-tank.menubar",
        "CFBundleVersion": "1.0.0",
        "CFBundleShortVersionString": "1.0.0",
        "LSUIElement": True,  # menu-bar-only app (no Dock icon)
        "NSBluetoothAlwaysUsageDescription": (
            "Clawd Tank uses Bluetooth to communicate with the ESP32 display."
        ),
    },
    "packages": ["clawd_tank_daemon", "clawd_tank_menubar"],
    "includes": ["rumps", "bleak", "objc"],
    "resources": ["clawd_tank_menubar/icons"],
}

setup(
    name="Clawd Tank",
    app=APP,
    data_files=DATA_FILES,
    options={"py2app": OPTIONS},
    setup_requires=["py2app"],
)
