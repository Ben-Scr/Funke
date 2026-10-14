"""The project's mobile orientation setting ("mobileOrientation" in project.json) for platform builders.

Left/Right name the side the top of the device points to, the same on every platform; the tables
translate that into each platform's own vocabulary. Keep the names in sync with ScreenOrientation in
Index-Engine/src/Core/WindowSpecification.hpp.
"""

from __future__ import annotations

import json
from pathlib import Path

ORIENTATIONS = (
    "Auto", "Landscape", "LandscapeLeft", "LandscapeRight", "Portrait", "PortraitUpsideDown",
    "PortraitAny", "AnyExceptUpsideDown", "Any",
)

# android:screenOrientation. Android's "landscape" is the device turned counter-clockwise (top left).
ANDROID_SCREEN_ORIENTATION = {
    "Auto": "unspecified",
    "Landscape": "sensorLandscape",
    "LandscapeLeft": "landscape",
    "LandscapeRight": "reverseLandscape",
    "Portrait": "portrait",
    "PortraitUpsideDown": "reversePortrait",
    "PortraitAny": "sensorPortrait",
    "AnyExceptUpsideDown": "sensor",
    "Any": "fullSensor",
}

# UISupportedInterfaceOrientations. UIKit names the side the home button / bottom edge is on, so the
# device turned counter-clockwise (top left) is UIInterfaceOrientationLandscapeRight.
_IOS_TOP_LEFT = "UIInterfaceOrientationLandscapeRight"
_IOS_TOP_RIGHT = "UIInterfaceOrientationLandscapeLeft"
_IOS_UPRIGHT = "UIInterfaceOrientationPortrait"
_IOS_UPSIDE_DOWN = "UIInterfaceOrientationPortraitUpsideDown"
IOS_INTERFACE_ORIENTATIONS = {
    "Auto": [_IOS_TOP_LEFT, _IOS_TOP_RIGHT, _IOS_UPRIGHT, _IOS_UPSIDE_DOWN],
    "Landscape": [_IOS_TOP_LEFT, _IOS_TOP_RIGHT],
    "LandscapeLeft": [_IOS_TOP_LEFT],
    "LandscapeRight": [_IOS_TOP_RIGHT],
    "Portrait": [_IOS_UPRIGHT],
    "PortraitUpsideDown": [_IOS_UPSIDE_DOWN],
    "PortraitAny": [_IOS_UPRIGHT, _IOS_UPSIDE_DOWN],
    "AnyExceptUpsideDown": [_IOS_TOP_LEFT, _IOS_TOP_RIGHT, _IOS_UPRIGHT],
    "Any": [_IOS_TOP_LEFT, _IOS_TOP_RIGHT, _IOS_UPRIGHT, _IOS_UPSIDE_DOWN],
}


def project_orientation(content_dir: Path) -> str:
    """The staged project's setting, with Auto resolved the way the runtime resolves it (landscape when
    the window is wider than tall) so the app starts the right way round instead of rotating."""
    try:
        project = json.loads((content_dir / "project.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        project = {}
    value = project.get("mobileOrientation")
    if value in ORIENTATIONS and value != "Auto":
        return value
    width, height = project.get("buildWidth", 1920), project.get("buildHeight", 1080)
    return "Landscape" if width > height else "PortraitAny"
