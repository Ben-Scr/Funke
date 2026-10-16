"""Offline tests for the Android builder's package contributions (manifests, Maven closures, R classes)."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts" / "platform"))
sys.path.insert(0, str(REPO_ROOT / "platforms" / "Index.Platform.Android" / "Builder"))

from index_platform_sdk import BuilderError  # noqa: E402
from index_platform_sdk.packages import ProjectPackage, parse_manifest, project_packages  # noqa: E402

import android_libraries  # noqa: E402
import android_manifest  # noqa: E402
import android_maven  # noqa: E402

ANDROID = "{http://schemas.android.com/apk/res/android}"

APP_MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.game.test">
    <uses-sdk android:minSdkVersion="26" android:targetSdkVersion="36" />
    <uses-permission android:name="android.permission.VIBRATE" />
    <application android:label="Game">
        <activity android:name="dev.index.player.IndexActivity" android:exported="true" />
    </application>
</manifest>
"""


class LuaManifestTests(unittest.TestCase):
    def write(self, folder: Path, text: str) -> Path:
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / "index-package.lua"
        path.write_text(text, encoding="utf-8")
        return path

    def test_reads_plain_data_with_comments(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(Path(tmp), """
-- header comment
--[[ block
comment ]]
return {
    name = "Index.Ads", version = '1.0.0',
    description = [[multi
line]],
    layers = { csharp = { sources = { "Source/**.cs" }, allow_unsafe = true } },
    android = {
        maven = { "a:b:1.0", "c:d:2.0", },
        placeholders = { admobAppId = "ca-app-pub-1~2", ["quoted.key"] = 3 },
    },
    dependencies = {},
}
""")
            manifest = parse_manifest(path)
        self.assertEqual(manifest["name"], "Index.Ads")
        self.assertEqual(manifest["description"], "multi\nline")
        self.assertTrue(manifest["layers"]["csharp"]["allow_unsafe"])
        self.assertEqual(manifest["android"]["maven"], ["a:b:1.0", "c:d:2.0"])
        self.assertEqual(manifest["android"]["placeholders"], {"admobAppId": "ca-app-pub-1~2", "quoted.key": 3})
        self.assertEqual(manifest["dependencies"], [])

    def test_rejects_code(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(Path(tmp), 'return { name = "A.B", version = os.getenv("X") }')
            with self.assertRaises(BuilderError):
                parse_manifest(path)

    def test_project_packages_follow_dependencies_and_prefer_the_project(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            engine, project = Path(tmp) / "engine", Path(tmp) / "project"
            self.write(engine / "packages" / "Index.Base", 'return { name = "Index.Base", version = "1.0.0" }')
            self.write(engine / "packages" / "Game.Ads", 'return { name = "Game.Ads", version = "0.0.1" }')
            self.write(project / "Packages" / "Game.Ads",
                       'return { name = "Game.Ads", version = "2.0.0", dependencies = { "Index.Base@^1.0.0" } }')
            (project / "project.json").write_text(json.dumps({"packages": ["Game.Ads", "Missing.Package"]}), encoding="utf-8")
            packages = project_packages(engine, project)
        self.assertEqual([p.name for p in packages], ["Index.Base", "Game.Ads"])
        self.assertEqual(packages[1].manifest["version"], "2.0.0")

    def test_premake_style_globs_recurse(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write(root, 'return { name = "Game.Ads", version = "1.0.0" }')
            for relative in ("Android/Java/dev/game/Plugin.java", "Android/Java/Top.java", "Android/Java/notes.txt"):
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
                (root / relative).write_text("x", encoding="utf-8")
            (root / "Android" / "AndroidManifest.xml").write_text("<manifest/>", encoding="utf-8")
            package = ProjectPackage("Game.Ads", root, {})
            java = [p.relative_to(root).as_posix() for p in package.files(["Android/Java/**.java"])]
            self.assertEqual(sorted(java), ["Android/Java/Top.java", "Android/Java/dev/game/Plugin.java"])
            self.assertEqual(len(package.files("Android/AndroidManifest.xml")), 1)
            with self.assertRaises(BuilderError):
                package.files(["Android/Missing.xml"])
            with self.assertRaises(BuilderError):
                package.files(["Android/Kotlin/**.kt"])


class ManifestMergeTests(unittest.TestCase):
    def merged(self, *others: str, placeholders: dict | None = None) -> ET.Element:
        text = android_manifest.merge(APP_MANIFEST, [(f"lib{i}", o) for i, o in enumerate(others)],
                                      {"applicationId": "com.game.test", **(placeholders or {})})
        return ET.fromstring(text)

    def test_merges_library_components_permissions_and_placeholders(self) -> None:
        library = """<manifest xmlns:android="http://schemas.android.com/apk/res/android"
            xmlns:tools="http://schemas.android.com/tools" package="com.lib">
            <uses-sdk android:minSdkVersion="21" />
            <uses-permission android:name="android.permission.INTERNET" />
            <uses-permission android:name="android.permission.VIBRATE" />
            <queries><package android:name="com.android.vending" /></queries>
            <application android:appComponentFactory="androidx.core.app.CoreComponentFactory" android:label="Lib">
                <provider android:name="androidx.startup.InitializationProvider"
                    android:authorities="${applicationId}.androidx-startup" tools:node="merge">
                    <meta-data android:name="first" android:value="androidx.startup" />
                </provider>
                <meta-data android:name="com.google.android.gms.ads.APPLICATION_ID" android:value="${admobAppId}" />
            </application>
        </manifest>"""
        second = """<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.lib2">
            <application>
                <provider android:name="androidx.startup.InitializationProvider" android:authorities="${applicationId}.androidx-startup">
                    <meta-data android:name="second" android:value="androidx.startup" />
                </provider>
            </application>
        </manifest>"""
        root = self.merged(library, second, placeholders={"admobAppId": "ca-app-pub-1~2"})
        permissions = [p.get(ANDROID + "name") for p in root.findall("uses-permission")]
        self.assertEqual(permissions.count("android.permission.VIBRATE"), 1)
        self.assertIn("android.permission.INTERNET", permissions)
        self.assertEqual(root.find("queries/package").get(ANDROID + "name"), "com.android.vending")
        application = root.find("application")
        self.assertEqual(application.get(ANDROID + "label"), "Game")
        self.assertEqual(application.get(ANDROID + "appComponentFactory"), "androidx.core.app.CoreComponentFactory")
        providers = application.findall("provider")
        self.assertEqual(len(providers), 1)
        self.assertEqual(providers[0].get(ANDROID + "authorities"), "com.game.test.androidx-startup")
        self.assertEqual(sorted(m.get(ANDROID + "name") for m in providers[0].findall("meta-data")), ["first", "second"])
        app_id = [m for m in application.findall("meta-data") if m.get(ANDROID + "name").endswith("APPLICATION_ID")]
        self.assertEqual(app_id[0].get(ANDROID + "value"), "ca-app-pub-1~2")
        self.assertEqual(list(root)[-1].tag, "application")
        self.assertNotIn("http://schemas.android.com/tools", ET.tostring(root, encoding="unicode"))

    def test_remove_and_unknown_placeholders_and_min_sdk(self) -> None:
        remover = """<manifest xmlns:android="http://schemas.android.com/apk/res/android"
            xmlns:tools="http://schemas.android.com/tools">
            <uses-permission android:name="android.permission.VIBRATE" tools:node="remove" />
        </manifest>"""
        self.assertNotIn("android.permission.VIBRATE",
                         [p.get(ANDROID + "name") for p in self.merged(remover).findall("uses-permission")])
        with self.assertRaises(android_manifest.ManifestError):
            self.merged('<manifest xmlns:android="http://schemas.android.com/apk/res/android"><application>'
                        '<meta-data android:name="x" android:value="${missing}" /></application></manifest>')
        with self.assertRaises(android_manifest.ManifestError):
            self.merged('<manifest xmlns:android="http://schemas.android.com/apk/res/android">'
                        '<uses-sdk android:minSdkVersion="30" /></manifest>')


class MavenTests(unittest.TestCase):
    def test_version_order(self) -> None:
        key = android_maven.version_key
        self.assertLess(key("1.0.0-beta05"), key("1.0.0"))
        self.assertLess(key("1.9.0"), key("1.10.1"))
        self.assertLess(key("1.0"), key("9999.0-empty-to-avoid-conflict-with-guava"))
        self.assertLess(key("2.3.1"), key("2.3.1.1"))

    def test_merge_keeps_the_newest_version_of_each_module(self) -> None:
        def artifact(text: str) -> android_maven.Artifact:
            return android_maven.Artifact(android_maven.Coordinate.parse(text), "aar", "https://x/" + text, "00")
        merged = android_maven.merge([[artifact("a:core:1.0.0"), artifact("a:ads:2.0.0")],
                                      [artifact("a:core:1.2.0"), artifact("b:billing:7.0.0")]])
        self.assertEqual([str(a.coordinate) for a in merged], ["a:core:1.2.0", "a:ads:2.0.0", "b:billing:7.0.0"])

    def test_lock_must_match_the_declared_roots(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Android").mkdir()
            lock = {"lockVersion": 1, "roots": ["a:b:1.0"], "artifacts": [
                {"id": "a:b:1.0", "type": "aar", "url": "https://x/b.aar", "sha256": "ab", "size": 3}]}
            (root / android_maven.LOCK_FILE).write_text(json.dumps(lock), encoding="utf-8")
            self.assertEqual(len(android_maven.read_lock(root, ["a:b:1.0"])), 1)
            self.assertIsNone(android_maven.read_lock(root, ["a:b:2.0"]))
            with self.assertRaises(BuilderError):
                android_maven.Coordinate.parse("a:b")


class LibraryOutputTests(unittest.TestCase):
    def test_r_classes_use_the_apps_ids_for_each_library_package(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "R.txt").write_text("int string app_name 0x7f010000\nint string lib_title 0x7f010001\n"
                                        "int[] styleable Banner { 0x7f020000, 0x7f020001 }\nint styleable Banner_size 1\n",
                                        encoding="utf-8")
            (root / "lib-R.txt").write_text("int string lib_title 0x0\nint[] styleable Banner { 0, 0 }\n"
                                            "int styleable Banner_size 0\nint string gone 0x0\n", encoding="utf-8")
            library = android_libraries.Library("x:lib:1", package="com.lib", r_txt=root / "lib-R.txt")
            sources = android_libraries.write_r_classes(root / "R.txt", [library], root / "gen")
            self.assertEqual(len(sources), 1)
            text = sources[0].read_text(encoding="utf-8")
        self.assertIn("package com.lib;", text)
        self.assertIn("public static final int lib_title = 0x7f010001;", text)
        self.assertIn("public static final int[] Banner = {0x7f020000, 0x7f020001};", text)
        self.assertIn("public static final int Banner_size = 1;", text)
        self.assertNotIn("app_name", text)
        self.assertNotIn("gone", text)

    def test_java_resources_merge_service_files(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            jars = []
            for index, provider in enumerate(("a.First", "b.Second")):
                jar = Path(tmp) / f"lib{index}.jar"
                with zipfile.ZipFile(jar, "w") as archive:
                    archive.writestr("META-INF/services/x.Factory", provider + "\n")
                    archive.writestr("META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n")
                    archive.writestr("x/Some.class", b"\xca\xfe")
                    archive.writestr("META-INF/lib.kotlin_module", str(index))
                jars.append(jar)
            files = android_libraries.java_resources(jars)
        self.assertEqual(files["META-INF/services/x.Factory"].decode().split(), ["a.First", "b.Second"])
        self.assertNotIn("META-INF/MANIFEST.MF", files)
        self.assertNotIn("x/Some.class", files)
        self.assertEqual(files["META-INF/lib.kotlin_module"], b"0")


if __name__ == "__main__":
    unittest.main()
