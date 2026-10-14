-- Android export (APK) for Index games.
--
-- Install this package to enable the Android tile in Build Profiles. Building needs the Android SDK
-- (platform 34+, build-tools), the NDK (r26+), a JDK 17+, CMake and Python 3.10+; the builder
-- finds Android Studio / standalone SDK installs on its own (`check` lists anything missing).
return {
    name         = "Index.Platform.Android",
    version      = "0.1.0",
    kind         = "platform",
    description  = "Build Index games for Android (arm64-v8a APK, Vulkan / OpenGL ES).",
    platform     = "Android",
    builder      = "Builder/build.py",
    hosts        = "windows,macos,linux",
    protocol     = "1",
    dependencies = { "Index.ScriptRuntime.Mono" },
}
