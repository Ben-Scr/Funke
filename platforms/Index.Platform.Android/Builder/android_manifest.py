"""Merges library and package manifests into the app manifest, like the Android Gradle plugin's merger.

The app manifest has the highest priority, then the others in the order given. Elements are matched by tag
and android:name; a lower-priority element only adds what the higher one lacks (attributes, children).
`tools:node="remove"` / `"replace"` are honoured, every other tools:* attribute is dropped from the output.
"""

from __future__ import annotations

import copy
import re
import xml.etree.ElementTree as ET

ANDROID = "http://schemas.android.com/apk/res/android"
TOOLS = "http://schemas.android.com/tools"
_NAME = f"{{{ANDROID}}}name"
_NODE = f"{{{TOOLS}}}node"

ET.register_namespace("android", ANDROID)
ET.register_namespace("tools", TOOLS)

# Top-level manifest children that libraries contribute; everything else (uses-sdk, instrumentation) stays the app's.
_MANIFEST_CHILDREN = {"uses-permission", "uses-permission-sdk-23", "permission", "permission-group", "permission-tree",
                      "uses-feature", "queries"}


class ManifestError(Exception):
    pass


def apply_placeholders(text: str, values: dict[str, str], origin: str) -> str:
    def replace(match: re.Match) -> str:
        name = match.group(1)
        if name not in values:
            raise ManifestError(f"{origin}: manifest placeholder ${{{name}}} has no value; set the build option '{name}'.")
        return values[name]
    return re.sub(r"\$\{([A-Za-z0-9_.]+)\}", replace, text)


def _key(element: ET.Element) -> tuple:
    if element.tag == "uses-feature":
        return (element.tag, element.get(_NAME) or element.get(f"{{{ANDROID}}}glEsVersion"))
    name = element.get(_NAME)
    if name is not None:
        return (element.tag, name)
    # Unnamed children (intent-filter, data, ...) only match an identical copy.
    return (element.tag, ET.tostring(_without_tools(copy.deepcopy(element))))


def _without_tools(element: ET.Element) -> ET.Element:
    for node in element.iter():
        for attribute in [a for a in node.attrib if a.startswith(f"{{{TOOLS}}}")]:
            del node.attrib[attribute]
    return element


def _merge_into(target: ET.Element, source: ET.Element, removed: set[tuple]) -> None:
    for attribute, value in source.attrib.items():
        if attribute.startswith(f"{{{TOOLS}}}"):
            continue
        if attribute == f"{{{ANDROID}}}required" and value == "true" and target.tag in ("uses-feature", "uses-library"):
            target.set(attribute, value)  # Required anywhere means required.
        target.attrib.setdefault(attribute, value)
    _merge_children(target, source, removed)


def _merge_children(target: ET.Element, source: ET.Element, removed: set[tuple], allowed: set[str] | None = None) -> None:
    existing = {_key(child): child for child in target}
    for child in source:
        if allowed is not None and child.tag not in allowed:
            continue
        key = _key(child)
        node = child.get(_NODE, "merge")
        if node in ("remove", "removeAll"):
            if key in existing:
                target.remove(existing.pop(key))
            removed.add(key)
            continue
        if key in removed:
            continue
        if key in existing:
            if node == "replace":
                index = list(target).index(existing[key])
                target.remove(existing[key])
                replacement = _without_tools(copy.deepcopy(child))
                target.insert(index, replacement)
                existing[key] = replacement
            else:
                _merge_into(existing[key], child, removed)
        else:
            added = _without_tools(copy.deepcopy(child))
            target.append(added)
            existing[key] = added


def merge(app_manifest: str, others: list[tuple[str, str]], placeholders: dict[str, str]) -> str:
    """app_manifest merged with (origin, text) manifests; ${name} placeholders are filled from placeholders."""
    app = ET.fromstring(app_manifest.encode("utf-8"))
    application = app.find("application")
    if application is None:
        raise ManifestError("The app manifest has no <application>.")
    uses_sdk = app.find("uses-sdk")
    app_min_sdk = int(uses_sdk.get(f"{{{ANDROID}}}minSdkVersion", "1")) if uses_sdk is not None else 1
    removed: set[tuple] = set()
    # The app's own tools:node="remove" entries block what libraries would add.
    for parent in (app, application):
        for child in list(parent):
            if child.get(_NODE) in ("remove", "removeAll"):
                removed.add(_key(child))
                parent.remove(child)
    for origin, text in others:
        try:
            other = ET.fromstring(apply_placeholders(text, placeholders, origin).encode("utf-8"))
        except ET.ParseError as error:
            raise ManifestError(f"{origin}: invalid AndroidManifest.xml: {error}")
        library_sdk = other.find("uses-sdk")
        library_min_sdk = library_sdk.get(f"{{{ANDROID}}}minSdkVersion", "1") if library_sdk is not None else "1"
        if library_min_sdk.isdigit() and int(library_min_sdk) > app_min_sdk:
            raise ManifestError(f"{origin} needs Android API {library_min_sdk}, the game supports API {app_min_sdk} and up.")
        queries = other.findall("queries")
        _merge_children(app, other, removed, _MANIFEST_CHILDREN - {"queries"})
        if queries:
            target = app.find("queries")
            if target is None:
                target = ET.SubElement(app, "queries")
            for query in queries:
                _merge_children(target, query, removed)
        library_application = other.find("application")
        if library_application is not None:
            for attribute, value in library_application.attrib.items():
                # The application class and label stay the app's.
                if not attribute.startswith(f"{{{TOOLS}}}") and attribute not in (_NAME, f"{{{ANDROID}}}label"):
                    application.attrib.setdefault(attribute, value)
            _merge_children(application, library_application, removed)
    _without_tools(app)
    # <queries> and permissions have to come before <application> for aapt2.
    children = list(app)
    for child in children:
        app.remove(child)
    order = {"uses-sdk": 0, "application": 2}
    for child in sorted(children, key=lambda c: order.get(c.tag, 1)):
        app.append(child)
    ET.indent(app, space="    ")
    return '<?xml version="1.0" encoding="utf-8"?>\n' + ET.tostring(app, encoding="unicode") + "\n"
