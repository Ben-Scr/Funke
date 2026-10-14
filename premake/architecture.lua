-- Host CPU architecture for every Index workspace (engine and project packages).
--
-- Index builds natively for the machine it runs on: x64 on Windows and Intel Linux/Macs, arm64 on
-- Apple Silicon and ARM Linux. Output folders use the names the engine probes at run time
-- (IndexProject.cpp GetNativeBuildArchitectureDirectory): bin/<cfg>-<system>-x86_64 or -arm64.

local function DetectIndexArchitecture()
    if os.target() == "windows" then
        -- Windows on ARM still builds the x64 editor (Dawn, the .NET host and the MSVC setup are x64 here).
        return "x86_64"
    end
    local machine = (os.outputof("uname -m") or ""):lower()
    if machine:find("arm64", 1, true) or machine:find("aarch64", 1, true) then
        return "arm64"
    end
    return "x86_64"
end

IndexArchitecture = DetectIndexArchitecture()
-- premake's own architecture token for the `architecture` call.
IndexPremakeArchitecture = IndexArchitecture == "arm64" and "ARM64" or "x64"
