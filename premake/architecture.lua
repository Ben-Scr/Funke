-- Host CPU architecture for every Funke workspace (engine and project packages).
--
-- Funke builds natively for the machine it runs on: x64 on Windows and Intel Linux/Macs, arm64 on
-- Apple Silicon and ARM Linux. Output folders use the names the engine probes at run time
-- (FunkeProject.cpp GetNativeBuildArchitectureDirectory): bin/<cfg>-<system>-x86_64 or -arm64.

local function DetectFunkeArchitecture()
    if os.target() == "windows" then
        -- Windows on ARM still builds the x64 editor (Dawn, the .NET host and the MSVC setup are x64 here).
        return "x86_64"
    end
    if os.target() == "macosx" then
        -- An Intel Premake running under Rosetta sees x86_64 from uname; dependencies are built natively.
        local appleArm = os.outputof("sysctl -n hw.optional.arm64 2>/dev/null") or ""
        if appleArm:match("^%s*1%s*$") then
            return "arm64"
        end
    end
    local machine = (os.outputof("uname -m") or ""):lower()
    if machine:find("arm64", 1, true) or machine:find("aarch64", 1, true) then
        return "arm64"
    end
    return "x86_64"
end

FunkeArchitecture = DetectFunkeArchitecture()
-- premake's own architecture token for the `architecture` call.
FunkePremakeArchitecture = FunkeArchitecture == "arm64" and "ARM64" or "x64"

function FunkeApplyArchitecture()
    architecture(FunkePremakeArchitecture)
    -- Premake beta5's gmake backend leaves ARM64 implicit; the compiler may also run under Rosetta.
    filter { "system:macosx", "language:C or C++" }
        buildoptions { "-arch " .. FunkeArchitecture }
        linkoptions { "-arch " .. FunkeArchitecture }
    filter {}
end
