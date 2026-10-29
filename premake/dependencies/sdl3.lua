-- SDL3 wiring: window, input, gamepads, clipboard, dialogs and the native
-- handles the WebGPU surface is created from.
--
-- SDL3 is built out-of-band by scripts/sdl/SetupSDL.py with its own CMake
-- (it needs per-platform configure checks premake can't express). Desktop
-- targets get a shared library so engine.dll and the consumer executables
-- (ImGui's SDL3 backend) share one copy of SDL's global state.
--
-- Headers come from the submodule so includes resolve before SetupSDL ran;
-- the link step then fails with a missing SDL3 lib, which is the cue to run it.

local function SDLTargetName()
    local target = os.target()
    if target == "windows" then return "windows" end
    if target == "macosx" then return "macos" end
    return "linux"
end

local sdlArchitecture = FunkeArchitecture == "arm64" and "arm64" or "x64"
local sdlInstallRoot = "External/SDL-build/" .. SDLTargetName() .. "-" .. sdlArchitecture .. "/install"

SDL3Layout = {}
SDL3Layout.InstallRoot = sdlInstallRoot
SDL3Layout.IncludeDirs = { "External/SDL/include" }
SDL3Layout.LibDirs = { sdlInstallRoot .. "/lib" }
SDL3Layout.Links = { "SDL3" }

if os.target() == "windows" then
    SDL3Layout.SharedLibFileName = "SDL3.dll"
    SDL3Layout.SharedLibPath = sdlInstallRoot .. "/bin/SDL3.dll"
elseif os.target() == "macosx" then
    SDL3Layout.SharedLibFileName = "libSDL3.0.dylib"
    SDL3Layout.SharedLibPath = sdlInstallRoot .. "/lib/libSDL3.0.dylib"
else
    SDL3Layout.SharedLibFileName = "libSDL3.so.0"
    SDL3Layout.SharedLibPath = sdlInstallRoot .. "/lib/libSDL3.so.0"
end

SDL3Layout.IsBuilt = os.isfile(path.join(ROOT_DIR or os.getcwd(), SDL3Layout.SharedLibPath))
if not SDL3Layout.IsBuilt and _ACTION ~= nil and _ACTION ~= "clean" then
    print("[premake] SDL3 is not built yet (" .. SDL3Layout.SharedLibPath .. "). Run: python scripts/sdl/SetupSDL.py")
end
