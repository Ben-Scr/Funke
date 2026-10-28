-- Self-test for premake/package-loader.lua's versioning rules.
--   vendor\bin\premake5.exe --file=premake/tests/package-loader-tests.lua funke-test-packages
-- Exits non-zero on the first failing check.

ROOT_DIR = path.getabsolute(_SCRIPT_DIR .. "/../..")
local Packages = dofile(path.join(ROOT_DIR, "premake/package-loader.lua"))
local SemVer = Packages.SemVer

local failures = 0
local function Check(condition, message)
    if not condition then
        failures = failures + 1
        print("FAILED: " .. message)
    end
end

local function InRange(range, version)
    local sets = assert(SemVer.ParseRange(range), "range " .. range)
    return SemVer.Satisfies(sets, assert(SemVer.Parse(version), "version " .. version))
end

local function WriteFile(filePath, text)
    os.mkdir(path.getdirectory(filePath))
    local file = assert(io.open(filePath, "wb"))
    file:write(text)
    file:close()
end

local function Manifest(name, version, extra)
    return "return { name = \"" .. name .. "\", version = \"" .. version .. "\", " .. (extra or "") ..
        " layers = { csharp = { sources = { \"Source/**.cs\" } } } }"
end

-- Every package under <root>/Packages, allow-listed in project.json; returns ok, sortedNamesOrError.
local function Resolve(packages, allowList, enginePackages)
    local root = path.join(os.getenv("TEMP") or os.getenv("TMPDIR") or "/tmp", "funke-loader-test-" .. tostring(os.time()) .. "-" .. tostring(math.random(1, 1000000)))
    for name, source in pairs(packages) do
        WriteFile(path.join(root, "Packages", name, "funke-package.lua"), source)
    end
    WriteFile(path.join(root, "project.json"), "{ \"packages\": [\"" .. table.concat(allowList, "\", \"") .. "\"] }")
    local previousRoot = ROOT_DIR
    if enginePackages then
        ROOT_DIR = path.join(root, "Engine")
        for name, source in pairs(enginePackages) do
            WriteFile(path.join(ROOT_DIR, "packages", name, "funke-package.lua"), source)
        end
    end
    local ok, result = pcall(Packages.ResolveProjectPackages, root)
    ROOT_DIR = previousRoot
    os.rmdir(root)
    if not ok then return false, tostring(result) end
    local names = {}
    for _, manifest in ipairs(result) do table.insert(names, manifest.name) end
    return true, names, result
end

newaction {
    trigger = "funke-test-packages",
    description = "Run package-loader versioning self-tests",
    execute = function()
        math.randomseed(os.time())

        Check(SemVer.Compare(SemVer.Parse("1.0.0-beta.2"), SemVer.Parse("1.0.0-beta.11")) < 0, "numeric pre-release identifiers compare numerically")
        Check(SemVer.Compare(SemVer.Parse("1.0.0-rc.1"), SemVer.Parse("1.0.0")) < 0, "pre-release sorts before release")
        Check(SemVer.Parse("1.2") == nil, "partial version is not a full version")
        Check(InRange("^1.2.3", "1.9.0") and not InRange("^1.2.3", "2.0.0"), "caret")
        Check(InRange("^0.2.3", "0.2.9") and not InRange("^0.2.3", "0.3.0"), "caret below 1.0")
        Check(InRange("~1.2.3", "1.2.9") and not InRange("~1.2.3", "1.3.0"), "tilde")
        Check(InRange("1.x", "1.7.0") and not InRange("1.x", "2.0.0"), "x-range")
        Check(InRange(">=1.0.0 <2.0.0", "1.5.0") and not InRange(">=1.0.0 <2.0.0", "2.0.0"), "comparator set")
        Check(InRange("1.0 - 2", "2.9.9"), "hyphen range")
        Check(InRange("^1.0.0 || ^3.0.0", "3.1.0") and not InRange("^1.0.0 || ^3.0.0", "2.0.0"), "or")
        Check(not InRange("^1.0.0", "1.5.0-beta") and InRange(">=1.5.0-beta <2.0.0", "1.5.0-rc.1"), "pre-release opt-in")
        Check(SemVer.ParseRange("^banana") == nil, "invalid range rejected")

        local ok, result = Resolve({
            ["Game.App"]  = Manifest("Game.App", "1.0.0", "dependencies = { \"Game.Ui@^2.0.0\" },"),
            ["Game.Ui"]   = Manifest("Game.Ui", "2.3.0", "dependencies = { { name = \"Game.Core\", version = \">=1.1.0\" } },"),
            ["Game.Core"] = Manifest("Game.Core", "1.2.0"),
        }, { "Game.App" })
        Check(ok and table.concat(result, ",") == "Game.Core,Game.Ui,Game.App", "dependencies first: " .. tostring(ok and table.concat(result, ",") or result))

        ok, result = Resolve({
            ["Game.App"] = Manifest("Game.App", "1.0.0", "dependencies = { [\"Game.Ui\"] = \"^3.0.0\" },"),
            ["Game.Ui"]  = Manifest("Game.Ui", "2.3.0"),
        }, { "Game.App" })
        Check(not ok and result:find("requires Game.Ui ^3.0.0, but 2.3.0 is installed", 1, true), "range mismatch reported: " .. tostring(result))

        ok, result = Resolve({ ["Game.Old"] = Manifest("Game.Old", "1.0.0", "engine = \"<1.0.0\",") }, { "Game.Old" })
        Check(not ok and result:find("needs engine <1.0.0", 1, true), "engine range enforced: " .. tostring(result))

        ok, result = Resolve({ ["Game.Bad"] = Manifest("Game.Bad", "1.0.0", "dependencies = { \"Game.X@^nope\" },") }, { "Game.Bad" })
        Check(not ok and result:find("invalid version range", 1, true), "invalid dependency range rejected: " .. tostring(result))

        local resolved
        ok, result, resolved = Resolve({
            ["Game.Shared"] = Manifest("Game.Shared", "2.0.0"),
        }, { "Game.Shared", "Game.Fallback" }, {
            ["Game.Shared"] = Manifest("Game.Shared", "1.0.0", "engine = \"<1.0.0\","),
            ["Game.Fallback"] = Manifest("Game.Fallback", "1.0.0", "dependencies = { \"Game.Shared@^2.0.0\" },"),
        })
        Check(ok and #resolved == 2 and resolved[1].version == "2.0.0",
            "project version overrides bundled version: " .. tostring(result))

        ok, result = Resolve({
            ["Game.Shared"] = Manifest("Game.Shared", "2.0.0"),
            ["Game.OtherFolder"] = Manifest("Game.Shared", "3.0.0"),
        }, { "Game.Shared" })
        Check(not ok and result:find("Duplicate package name", 1, true), "duplicate project names still fail")

        ok, result = Resolve({ ["Game.Shared"] = Manifest("Game.Shared", "2.0.0") }, { "Game.Shared" }, {
            ["Game.Shared"] = Manifest("Game.Shared", "1.0.0"),
            ["Game.OtherFolder"] = Manifest("Game.Shared", "1.1.0"),
        })
        Check(not ok and result:find("Duplicate package name", 1, true), "duplicate engine names still fail")

        if failures > 0 then
            error(failures .. " package-loader check(s) failed", 0)
        end
        print("package-loader tests passed")
    end,
}
