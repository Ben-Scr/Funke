-- Helpers that keep the build scripts working across premake 5.0.0-beta5 (vendored for Windows,
-- used by CI) and newer releases (Homebrew), which replaced several `flags` values with
-- dedicated fields and then dropped the flags.

function FunkeMultiProcessorCompile()
    if multiprocessorcompile then
        multiprocessorcompile "On"
    else
        flags { "MultiProcessorCompile" }
    end
end

-- Visual Studio 18 and 2022 ship the same v143 toolset from different folders, and #pragma once is
-- path-based: a PCH built by one breaks the other's compiles (C2375 "__builtin_assume_aligned") and the
-- project's .lib stays stale (LNK2019 in its consumers). Directory.Build.props sets FunkeIntDirSuffix
-- for every Visual Studio except 2022, so each install keeps its own objects, PCH and tlogs.
do
    local ok = pcall(require, "vstudio")
    local vc = ok and premake.vstudio and premake.vstudio.vc2010
    if vc and vc.intDir and vc.element and premake.vstudio.path then
        premake.override(vc, "intDir", function(base, cfg)
            vc.element("IntDir", nil, "%s$(FunkeIntDirSuffix)\\", premake.vstudio.path(cfg, cfg.objdir))
        end)
    end
end

function FunkeNoPCH()
    if enablepch then
        enablepch "Off"
    else
        flags { "NoPCH" }
    end
end

local function QuoteShellArgument(value)
    if os.host() == "windows" then
        return '"' .. value:gsub('"', '\\"') .. '"'
    end
    return "'" .. value:gsub("'", "'\\''") .. "'"
end

local function IsUsablePython(command)
    local _, exitCode = os.outputof(command ..
        " -c \"import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)\"")
    return exitCode == 0
end

local cachedPython = nil

-- Python 3.10+ for premake-time steps and build commands. Setup passes the interpreter it found as
-- FUNKE_PYTHON; Homebrew's python@3.x only installs python3.x, and macOS' own python3 is 3.9.
function FunkePython()
    if cachedPython then
        return cachedPython
    end
    local fromSetup = os.getenv("FUNKE_PYTHON")
    if fromSetup and fromSetup ~= "" then
        cachedPython = QuoteShellArgument(fromSetup)
    elseif os.host() == "windows" then
        cachedPython = "python"
    else
        cachedPython = "python3"
        for _, candidate in ipairs({ "python3", "python3.14", "python3.13", "python3.12", "python3.11", "python3.10" }) do
            if IsUsablePython(candidate) then
                cachedPython = candidate
                break
            end
        end
    end
    return cachedPython
end
