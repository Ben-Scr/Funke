-- Funke package system — Phase A loader.
--
-- Discovers `funke-package.lua` manifests under `packages/<Name>/`, validates them,
-- topologically sorts by dependency, and registers Premake projects:
--
--   - native  / native_standalone  →  Pkg.<Name>.Native  (C++ SharedLib)
--   - csharp                       →  Pkg.<Name>          (C# SharedLib)
--
-- A single package may declare any combination of the three layers. When both
-- native and native_standalone are present they are merged into a single
-- Pkg.<Name>.Native (and that lib then links the engine, since `native` does).
--
-- Layer naming history
-- --------------------
-- The layers used to be called `engine_core` (links the engine) and
-- `standalone_cpp` (does not link the engine). Those names led to two recurring
-- confusions: `engine_core` was misread as "code that belongs to the engine
-- core" and `standalone_cpp` was wordy when its sibling was just `csharp`.
-- The canonical names are now `native` and `native_standalone`. The old names
-- still work — manifests using them are normalised + warned at validation time
-- so existing project-local packages don't break — but new manifests should
-- use the canonical names.
--
-- Manifest schema (returned by funke-package.lua via `return { ... }`):
--
--   name          string   required, unique
--   version       string   required
--   description   string   optional
--   layers        table    required, at least one of native / native_standalone / csharp
--                          each layer:
--                              sources           list of file/glob patterns relative to the package dir
--                              includes          optional list of include dirs (relative)
--                              private_includes  optional list of include dirs (relative)
--                          csharp may additionally declare:
--                              pinvoke_dll       string identifying the package's own native lib
--                              allow_unsafe      true to enable `clr "Unsafe"` for fixed-pin pinvoke
--                              nuget             list of "PackageId:Version" strings — emitted as
--                                                <PackageReference> entries in the generated csproj.
--                                                Use sparingly; matches the engine's "no mandatory
--                                                dependencies" rule (the package opts in, the user
--                                                opts into the package).
--   dependencies  table    optional; each entry is a package name ("Funke.Foo"), "Funke.Foo@^1.2.0",
--                          { name = "Funke.Foo", version = "^1.2.0" }, or a map entry ["Funke.Foo"] = "^1.2.0"
--   engine        string   optional engine version range, e.g. ">=2026.1.0 <2027.0.0"
--   modules       table    optional engine modules the package builds on, e.g. { "Funke.2D" }
--   android       table    optional; read by the Android platform builder, not here: java, maven, manifest,
--                          placeholders (platforms/Funke.Platform.Android/Builder/android_libraries.py)
--
-- `version` should be MAJOR.MINOR.PATCH (SemVer). Ranges follow npm syntax: ^ ~ x-ranges, comparators,
-- hyphen ranges and ||. The C++ side (Funke-Engine/src/Packages/SemanticVersion.cpp) implements the same rules.

local PackageSystem = {}

local function PackageError(msg)
    error("[Funke Packages] " .. msg, 0)
end

-- ── Semantic versions and ranges ───────────────────────────────────────────
local SemVer = {}

local function Trim(text)
    return (text:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function ParseNumber(text)
    if not text:match("^%d+$") then return nil end
    if #text > 1 and text:sub(1, 1) == "0" then return nil end
    return tonumber(text)
end

local function SplitDots(text)
    local parts = {}
    for part in (text .. "."):gmatch("([^%.]*)%.") do table.insert(parts, part) end
    return parts
end

function SemVer.Parse(text)
    if type(text) ~= "string" then return nil end
    text = Trim(text):gsub("^[vV]", "")
    local plus = text:find("+", 1, true)
    if plus then text = text:sub(1, plus - 1) end
    local pre = {}
    local dash = text:find("-", 1, true)
    if dash then
        local preText = text:sub(dash + 1)
        text = text:sub(1, dash - 1)
        for _, identifier in ipairs(SplitDots(preText)) do
            if identifier == "" or identifier:match("[^%w%-]") then return nil end
            if identifier:match("^%d+$") and #identifier > 1 and identifier:sub(1, 1) == "0" then return nil end
            table.insert(pre, identifier)
        end
    end
    local major, minor, patch = text:match("^(%d+)%.(%d+)%.(%d+)$")
    if not major then return nil end
    major, minor, patch = ParseNumber(major), ParseNumber(minor), ParseNumber(patch)
    if not (major and minor and patch) then return nil end
    return { major, minor, patch, pre = pre }
end

local function CompareIdentifiers(a, b)
    local an, bn = a:match("^%d+$"), b:match("^%d+$")
    if an and bn then
        local x, y = tonumber(a), tonumber(b)
        if x == y then return 0 end
        return x < y and -1 or 1
    end
    if an then return -1 end
    if bn then return 1 end
    if a == b then return 0 end
    return a < b and -1 or 1
end

function SemVer.Compare(a, b)
    for i = 1, 3 do
        if a[i] ~= b[i] then return a[i] < b[i] and -1 or 1 end
    end
    local ap, bp = a.pre or {}, b.pre or {}
    if #ap == 0 or #bp == 0 then
        if #ap == #bp then return 0 end
        return #ap == 0 and 1 or -1
    end
    for i = 1, math.min(#ap, #bp) do
        local result = CompareIdentifiers(ap[i], bp[i])
        if result ~= 0 then return result end
    end
    if #ap == #bp then return 0 end
    return #ap < #bp and -1 or 1
end

function SemVer.ToString(v)
    local text = v[1] .. "." .. v[2] .. "." .. v[3]
    if v.pre and #v.pre > 0 then text = text .. "-" .. table.concat(v.pre, ".") end
    return text
end

-- Partial versions in ranges: "1", "1.2", "1.x", "*". Returns { numbers..., given = n } or nil.
local function ParsePartial(text)
    text = text:gsub("^[vV]", "")
    if text == "" then return nil end
    local full = SemVer.Parse(text)
    if full then return { full[1], full[2], full[3], given = 3, full = full } end
    local numbers, given, sawWildcard = { 0, 0, 0 }, 0, false
    local parts = SplitDots(text)
    if #parts > 3 then return nil end
    for _, part in ipairs(parts) do
        if part == "x" or part == "X" or part == "*" then
            sawWildcard = true
        else
            local number = ParseNumber(part)
            if sawWildcard or not number then return nil end
            given = given + 1
            numbers[given] = number
        end
    end
    return { numbers[1], numbers[2], numbers[3], given = given, full = { numbers[1], numbers[2], numbers[3], pre = {} } }
end

local function V(major, minor, patch, lowestPre)
    return { major, minor, patch, pre = lowestPre and { "0" } or {} }
end

local function UpperBound(partial)
    if partial.given == 1 then return V(partial[1] + 1, 0, 0, true) end
    return V(partial[1], partial[2] + 1, 0, true)
end

-- Returns a list of comparator sets ({ op, version, synthetic }) or nil, errorMessage.
function SemVer.ParseRange(text)
    text = Trim(text or "")
    if text == "" or text == "*" then
        return { { { op = ">=", v = V(0, 0, 0), synthetic = true } } }
    end
    local sets = {}
    for setText in (text .. "||"):gmatch("(.-)||") do
        setText = Trim(setText)
        local set = {}
        if setText == "" or setText == "*" then
            table.insert(set, { op = ">=", v = V(0, 0, 0), synthetic = true })
        else
            local raw = {}
            for token in setText:gmatch("%S+") do table.insert(raw, token) end
            local tokens = {}
            local i = 1
            while i <= #raw do
                local token = raw[i]
                if (token == ">" or token == ">=" or token == "<" or token == "<=" or token == "=" or token == "^" or token == "~") and raw[i + 1] then
                    token = token .. raw[i + 1]
                    i = i + 1
                end
                table.insert(tokens, token)
                i = i + 1
            end

            if #tokens == 3 and tokens[2] == "-" then
                local low, high = ParsePartial(tokens[1]), ParsePartial(tokens[3])
                if not low or not high then return nil, "invalid hyphen range '" .. setText .. "'" end
                table.insert(set, { op = ">=", v = low.full })
                if high.given == 3 then
                    table.insert(set, { op = "<=", v = high.full })
                elseif high.given > 0 then
                    table.insert(set, { op = "<", v = UpperBound(high), synthetic = true })
                end
            else
                for _, token in ipairs(tokens) do
                    local op, body = token:match("^([<>=^~]*)(.*)$")
                    if not (op == "" or op == ">=" or op == "<=" or op == ">" or op == "<" or op == "=" or op == "^" or op == "~") then
                        return nil, "invalid operator in '" .. token .. "'"
                    end
                    local partial = ParsePartial(body)
                    if not partial then return nil, "invalid version '" .. body .. "' in range '" .. text .. "'" end
                    local major, minor, patch = partial[1], partial[2], partial[3]
                    if op == "^" then
                        table.insert(set, { op = ">=", v = partial.full })
                        if partial.given > 0 then
                            local upper
                            if major > 0 or partial.given == 1 then upper = V(major + 1, 0, 0, true)
                            elseif minor > 0 or partial.given == 2 then upper = V(0, minor + 1, 0, true)
                            else upper = V(0, 0, patch + 1, true) end
                            table.insert(set, { op = "<", v = upper, synthetic = true })
                        end
                    elseif op == "~" then
                        table.insert(set, { op = ">=", v = partial.full })
                        if partial.given > 0 then
                            table.insert(set, { op = "<", v = partial.given == 1 and V(major + 1, 0, 0, true) or V(major, minor + 1, 0, true), synthetic = true })
                        end
                    elseif partial.given == 3 then
                        table.insert(set, { op = op == "" and "=" or op, v = partial.full })
                    elseif partial.given == 0 then
                        if op == "<" or op == ">" then
                            table.insert(set, { op = "<", v = V(0, 0, 0, true), synthetic = true })
                        else
                            table.insert(set, { op = ">=", v = V(0, 0, 0), synthetic = true })
                        end
                    elseif op == "" or op == "=" then
                        table.insert(set, { op = ">=", v = partial.full })
                        table.insert(set, { op = "<", v = UpperBound(partial), synthetic = true })
                    elseif op == ">" then
                        local bound = UpperBound(partial)
                        table.insert(set, { op = ">=", v = V(bound[1], bound[2], bound[3]) })
                    elseif op == ">=" then
                        table.insert(set, { op = ">=", v = partial.full })
                    elseif op == "<" then
                        table.insert(set, { op = "<", v = V(major, minor, patch, true), synthetic = true })
                    elseif op == "<=" then
                        table.insert(set, { op = "<", v = UpperBound(partial), synthetic = true })
                    end
                end
            end
        end
        if #set == 0 then return nil, "empty comparator set in range '" .. text .. "'" end
        table.insert(sets, set)
    end
    return sets
end

function SemVer.Satisfies(sets, version)
    for _, set in ipairs(sets) do
        local all = true
        for _, comparator in ipairs(set) do
            local c = SemVer.Compare(version, comparator.v)
            local op = comparator.op
            local ok = (op == "<" and c < 0) or (op == "<=" and c <= 0) or (op == ">" and c > 0)
                or (op == ">=" and c >= 0) or (op == "=" and c == 0)
            if not ok then all = false break end
        end
        if all then
            if #version.pre == 0 then return true end
            -- Pre-releases need an explicit opt-in on the same MAJOR.MINOR.PATCH.
            for _, comparator in ipairs(set) do
                if not comparator.synthetic and #comparator.v.pre > 0
                    and comparator.v[1] == version[1] and comparator.v[2] == version[2] and comparator.v[3] == version[3] then
                    return true
                end
            end
        end
    end
    return false
end

PackageSystem.SemVer = SemVer

-- FNK_VERSION from the generated build metadata; nil when it is not available (pre-release suffix dropped).
local function ReadEngineVersion()
    local metadataPath = path.join(ROOT_DIR, "Funke-Engine/src/Core/BuildMetadata.generated.hpp")
    local file = io.open(metadataPath, "rb")
    if not file then return nil end
    local text = file:read("*all")
    file:close()
    local version = SemVer.Parse(text:match('#define%s+FNK_VERSION%s+"([^"]+)"') or "")
    if not version then return nil end
    return V(version[1], version[2], version[3])
end

local k_EngineVersion = nil
local function GetEngineVersion()
    if k_EngineVersion == nil then
        k_EngineVersion = ReadEngineVersion() or false
    end
    return k_EngineVersion or nil
end

-- Canonical layer names. Old names (engine_core, standalone_cpp) are accepted
-- but normalised to the canonical form before any other code reads them.
local k_AllowedLayers = { native = true, native_standalone = true, csharp = true }
local k_LayerAliases = {
    engine_core    = "native",
    standalone_cpp = "native_standalone",
}

-- Walk the layers table, rename any deprecated keys to their canonical form,
-- and emit a one-time warning per occurrence. Returns the (possibly mutated)
-- layers table — call this BEFORE ValidateManifest so the rest of the loader
-- only ever sees `native` / `native_standalone` / `csharp`.
local function NormaliseLayerNames(manifest)
    if type(manifest.layers) ~= "table" then return end
    for oldName, newName in pairs(k_LayerAliases) do
        local spec = manifest.layers[oldName]
        if spec ~= nil then
            if manifest.layers[newName] ~= nil then
                PackageError("Package '" .. tostring(manifest.name) ..
                    "' declares both '" .. oldName .. "' (deprecated alias) and '" ..
                    newName .. "' (canonical). Pick one.")
            end
            print("[Funke Packages] Package '" .. tostring(manifest.name) ..
                "' uses deprecated layer name '" .. oldName .. "'. Rename to '" ..
                newName .. "' in funke-package.lua. (Old name still works for now.)")
            manifest.layers[newName] = spec
            manifest.layers[oldName] = nil
        end
    end
end

-- PascalCase.PascalCase[.PascalCase…]. Same shape as scripts/packages/NewPackage.py's
-- regex. Anything outside this character set breaks downstream as a project
-- name, folder name, or DllImport library name — fail loudly at validation
-- time rather than silently mid-build.
local function IsValidPackageName(name)
    if type(name) ~= "string" or #name == 0 then return false end
    -- Lua patterns don't have alternation, so structure the check by hand:
    -- must start with [A-Z], contain only [A-Za-z0-9.], and have at least
    -- one '.' separator with non-empty segments either side.
    if not name:match("^[A-Z]") then return false end
    if name:match("[^A-Za-z0-9%.]") then return false end
    if name:match("%.%.") then return false end             -- no empty segment
    if name:sub(-1) == "." then return false end            -- no trailing dot
    if not name:find(".", 1, true) then return false end    -- need at least one dot
    -- Every segment must start with an uppercase letter.
    for segment in name:gmatch("[^%.]+") do
        if not segment:match("^[A-Z]") then return false end
    end
    return true
end

local function ValidateManifest(manifest, manifestPath)
    if type(manifest) ~= "table" then
        PackageError("Manifest at '" .. manifestPath .. "' did not return a table.")
    end
    if type(manifest.name) ~= "string" or manifest.name == "" then
        PackageError("Manifest at '" .. manifestPath .. "' is missing required string field 'name'.")
    end
    if not IsValidPackageName(manifest.name) then
        PackageError("Manifest at '" .. manifestPath ..
            "' has invalid 'name' = '" .. tostring(manifest.name) ..
            "'. Expected PascalCase.PascalCase[.PascalCase…] with only [A-Za-z0-9.] (e.g. 'Funke.Foo', 'MyGame.Loot').")
    end
    if type(manifest.version) ~= "string" then
        PackageError("Package '" .. manifest.name .. "' is missing required string field 'version'.")
    end
    if type(manifest.layers) ~= "table" then
        PackageError("Package '" .. manifest.name .. "' must declare a 'layers' table.")
    end

    local layerCount = 0
    for layerName, layerSpec in pairs(manifest.layers) do
        if not k_AllowedLayers[layerName] then
            PackageError("Package '" .. manifest.name .. "' declares unknown layer '" ..
                tostring(layerName) .. "'. Allowed: native, native_standalone, csharp " ..
                "(deprecated aliases: engine_core → native, standalone_cpp → native_standalone).")
        end
        if type(layerSpec) ~= "table" then
            PackageError("Package '" .. manifest.name .. "' layer '" .. layerName .. "' must be a table.")
        end
        if type(layerSpec.sources) ~= "table" or #layerSpec.sources == 0 then
            PackageError("Package '" .. manifest.name .. "' layer '" .. layerName ..
                "' must declare at least one source pattern in 'sources'.")
        end
        layerCount = layerCount + 1
    end

    if layerCount == 0 then
        PackageError("Package '" .. manifest.name .. "' declares zero layers; at least one is required.")
    end

    -- `native` links the engine via EditorRuntimeCommon; `native_standalone`
    -- explicitly does not. Combining both into the same Pkg.<Name>.Native
    -- silently links the engine into the "standalone" sources, breaking
    -- the layer's whole reason to exist. scripts/packages/NewPackage.py already
    -- rejects this combo; mirror that here so hand-written manifests fail
    -- the same way.
    if manifest.layers.native and manifest.layers.native_standalone then
        PackageError("Package '" .. manifest.name ..
            "' declares both 'native' and 'native_standalone' layers. They merge into a single Pkg.<Name>.Native and cannot coexist — `native` links the engine while `native_standalone` must not. Pick one.")
    end

    if manifest.layers.csharp and manifest.layers.csharp.pinvoke_dll then
        if not (manifest.layers.native or manifest.layers.native_standalone) then
            PackageError("Package '" .. manifest.name ..
                "' declares 'pinvoke_dll' on csharp layer but has no native (native / native_standalone) layer to bridge to.")
        end
        -- The native sibling always builds as Pkg.<Name>.Native, and the C#
        -- DllImport + PackageNativeResolver resolve the bridge by exactly that
        -- name. A mismatched pinvoke_dll compiles fine but fails at RUNTIME
        -- (DllNotFound / EntryPointNotFound), so reject it here at premake time.
        local expectedPinvoke = "Pkg." .. manifest.name .. ".Native"
        if manifest.layers.csharp.pinvoke_dll ~= expectedPinvoke then
            PackageError("Package '" .. manifest.name ..
                "' has pinvoke_dll = '" .. tostring(manifest.layers.csharp.pinvoke_dll) ..
                "' but its native sibling builds as '" .. expectedPinvoke ..
                "'. Set pinvoke_dll to exactly that, or the C# DllImport won't resolve at runtime.")
        end
    end

    if manifest.layers.csharp and manifest.layers.csharp.nuget ~= nil then
        if type(manifest.layers.csharp.nuget) ~= "table" then
            PackageError("Package '" .. manifest.name ..
                "' csharp layer has invalid 'nuget' field; expected a list of \"PackageId:Version\" strings.")
        end
        for _, ref in ipairs(manifest.layers.csharp.nuget) do
            if type(ref) ~= "string" or not ref:find(":", 1, true) then
                PackageError("Package '" .. manifest.name ..
                    "' csharp layer has invalid nuget entry '" .. tostring(ref) ..
                    "'. Each entry must be a \"PackageId:Version\" string.")
            end
        end
    end

    if manifest.dependencies ~= nil and type(manifest.dependencies) ~= "table" then
        PackageError("Package '" .. manifest.name .. "' has invalid 'dependencies' field; expected a table or nil.")
    end

    -- Engine modules (Funke-Engine/EngineModules.json) the package builds on; the editor enables them on install.
    if manifest.modules ~= nil then
        if type(manifest.modules) ~= "table" then
            PackageError("Package '" .. manifest.name .. "' has invalid 'modules' field; expected a list such as { \"Funke.2D\" }.")
        end
        local known = { ["Funke.2D"] = true, ["Funke.3D"] = true }
        for _, module in ipairs(manifest.modules) do
            if type(module) ~= "string" or not known[module] then
                PackageError("Package '" .. manifest.name .. "' lists unknown engine module '" .. tostring(module) ..
                    "' in 'modules' (known: Funke.2D, Funke.3D).")
            end
        end
    end
end

-- Rewrites `dependencies` into a plain name list (what the rest of the loader reads) and keeps the
-- version ranges in `DependencySpecs`. Also validates `version` and the engine range.
local function NormaliseVersioning(manifest)
    local specs = {}
    local seen = {}
    local function Add(name, range)
        if type(name) ~= "string" or not IsValidPackageName(name) then
            PackageError("Package '" .. manifest.name .. "' has invalid dependency name '" .. tostring(name) .. "'.")
        end
        if name == manifest.name then
            PackageError("Package '" .. manifest.name .. "' declares itself as a dependency.")
        end
        if seen[name] then
            PackageError("Package '" .. manifest.name .. "' lists dependency '" .. name .. "' twice.")
        end
        range = (range == nil or range == "") and "*" or range
        if type(range) ~= "string" then
            PackageError("Package '" .. manifest.name .. "' dependency '" .. name .. "' has a non-string version range.")
        end
        local parsed, rangeError = SemVer.ParseRange(range)
        if not parsed then
            PackageError("Package '" .. manifest.name .. "' dependency '" .. name .. "' has an invalid version range: " .. rangeError)
        end
        seen[name] = true
        table.insert(specs, { name = name, range = range, parsed = parsed })
    end

    if manifest.dependencies then
        for _, entry in ipairs(manifest.dependencies) do
            if type(entry) == "string" then
                local name, range = entry:match("^([^@]+)@(.*)$")
                Add(name or entry, range)
            elseif type(entry) == "table" then
                Add(entry.name, entry.version)
            else
                PackageError("Package '" .. manifest.name .. "' has a dependency entry that is neither a string nor a table.")
            end
        end
        for key, value in pairs(manifest.dependencies) do
            if type(key) == "string" then Add(key, value) end
        end
    end

    manifest.DependencySpecs = specs
    manifest.dependencies = {}
    for _, spec in ipairs(specs) do table.insert(manifest.dependencies, spec.name) end

    manifest.ParsedVersion = SemVer.Parse(manifest.version)
    if not manifest.ParsedVersion then
        print("[Funke Packages] WARNING: package '" .. manifest.name .. "' has version '" .. manifest.version ..
            "', which is not MAJOR.MINOR.PATCH; packages that depend on it with a version range will fail.")
    end

    local engineRange = manifest.engine or manifest.engineVersion
    if engineRange ~= nil and engineRange ~= "" then
        local parsed, rangeError = SemVer.ParseRange(engineRange)
        if not parsed then
            PackageError("Package '" .. manifest.name .. "' has an invalid 'engine' range: " .. rangeError)
        end
        local engineVersion = GetEngineVersion()
        if engineVersion and not SemVer.Satisfies(parsed, engineVersion) then
            PackageError("Package '" .. manifest.name .. "' needs engine " .. engineRange ..
                ", but this is Funke " .. SemVer.ToString(engineVersion) .. ". Install a release of the package made for this engine.")
        end
    end
end

local function LoadManifestsFromRoot(searchRoot, manifests, byName)
    if not os.isdir(searchRoot) then
        return
    end

    local entries = os.matchdirs(path.join(searchRoot, "*"))
    for _, entry in ipairs(entries) do
        local manifestPath = path.join(entry, "funke-package.lua")
        if os.isfile(manifestPath) then
            local manifest = dofile(manifestPath)
            -- Rewrite deprecated layer names to canonical form before any
            -- validator / dispatcher reads them. Warns on rewrite.
            NormaliseLayerNames(manifest)
            ValidateManifest(manifest, manifestPath)
            NormaliseVersioning(manifest)

            if byName[manifest.name] then
                PackageError("Duplicate package name '" .. manifest.name ..
                    "' (first at '" .. byName[manifest.name].PackageDir ..
                    "', second at '" .. entry .. "').")
            end

            manifest.PackageDir = entry
            manifest.ManifestPath = manifestPath
            table.insert(manifests, manifest)
            byName[manifest.name] = manifest
        end
    end
end

local function TopoSort(manifests, byName)
    local sorted = {}
    local visiting = {}
    local visited = {}

    local function Visit(name, dependentName)
        -- Cycle check MUST come before the visited short-circuit. A self-
        -- dependency (deps = {Self}) recurses with visiting[name] already
        -- set; checking visited first would return early on the second
        -- entry and silently accept the cycle.
        if visiting[name] then
            PackageError("Cyclic package dependency involving '" .. name .. "'.")
        end
        if visited[name] then return end

        local m = byName[name]
        if not m then
            local dependent = dependentName and ("'" .. dependentName .. "' depends on ") or ""
            PackageError(dependent .. "missing package '" .. name .. "'.")
        end

        visiting[name] = true

        for _, spec in ipairs(m.DependencySpecs or {}) do
            Visit(spec.name, name)
            if spec.range ~= "*" then
                local dependency = byName[spec.name]
                if not dependency.ParsedVersion then
                    PackageError("Package '" .. name .. "' requires " .. spec.name .. " " .. spec.range ..
                        ", but its version '" .. tostring(dependency.version) .. "' is not MAJOR.MINOR.PATCH.")
                end
                if not SemVer.Satisfies(spec.parsed, dependency.ParsedVersion) then
                    PackageError("Package '" .. name .. "' requires " .. spec.name .. " " .. spec.range ..
                        ", but " .. dependency.version .. " is installed (" .. dependency.PackageDir .. ").")
                end
            end
        end

        visiting[name] = nil
        visited[name] = true
        table.insert(sorted, m)
    end

    for _, m in ipairs(manifests) do
        Visit(m.name, nil)
    end

    return sorted
end

local function ResolvePaths(packageDir, patterns)
    local resolved = {}
    if patterns then
        for _, pat in ipairs(patterns) do
            table.insert(resolved, path.join(packageDir, pat))
        end
    end
    return resolved
end

local function AppendAll(dst, src)
    for _, value in ipairs(src) do
        table.insert(dst, value)
    end
end

local function PackageProjectLocation(projectName)
    local root = FunkePackageProjectLocationRoot or path.join(ROOT_DIR, "premake/generated")
    return path.join(root, projectName)
end

local function PackageTargetDirectory()
    local root = FunkePackageTargetRoot or path.join(ROOT_DIR, "bin")
    return path.join(root, outputdir, "%{prj.name}")
end

local function PackageObjectDirectory()
    local root = FunkePackageObjectRoot or path.join(ROOT_DIR, "bin-int")
    return path.join(root, outputdir, "%{prj.name}")
end

local function PowerShellLiteral(value)
    return "'" .. value:gsub("'", "''") .. "'"
end

local function NativeAbiMetadataCommand(hasEngineLinked)
    local sidecar = PowerShellLiteral("%{cfg.buildtarget.abspath}.funkeabi")
    local boundary = hasEngineLinked and "engine_cpp" or "standalone_c"
    local lines = "$lines=@('schema=1','boundary=" .. boundary .. "')"

    if hasEngineLinked then
        local engineDll = PowerShellLiteral(path.join(
            ROOT_DIR, "bin", outputdir, "Funke-Engine", "Funke-Engine.dll"))
        lines = "$engine=" .. engineDll ..
            ";if(!(Test-Path -LiteralPath $engine)){throw 'Matching Funke-Engine.dll was not built'}" ..
            ";$hash=(Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant()" ..
            ";$lines=@('schema=1','boundary=engine_cpp',('engine_sha256='+$hash))"
    end

    return "powershell -NoProfile -ExecutionPolicy Bypass -Command \"" .. lines ..
        ";[IO.File]::WriteAllLines(" .. sidecar .. ",$lines,[Text.Encoding]::ASCII)\""
end

local function PosixNativeAbiMetadataCommand(hasEngineLinked)
    local command = 'python3 "' .. path.join(ROOT_DIR, "scripts/packages/WriteNativeAbi.py") ..
        '" "%{cfg.buildtarget.abspath}"'
    if not hasEngineLinked then
        return command .. " standalone_c"
    end
    local engineLibrary = os.target() == "macosx" and "libIndex-Engine.dylib" or "libIndex-Engine.so"
    return command .. ' engine_cpp "' .. path.join(ROOT_DIR, "bin", outputdir, "Funke-Engine", engineLibrary) .. '"'
end

local function RegisterNativeProject(manifest)
    local layers = manifest.layers
    local hasEngineLinked   = layers.native ~= nil
    local hasEngineUnlinked = layers.native_standalone ~= nil

    if not hasEngineLinked and not hasEngineUnlinked then
        return
    end

    local nativeName = "Pkg." .. manifest.name .. ".Native"

    local sources = {}
    local includes = {}

    local function AbsorbLayer(layer)
        AppendAll(sources, ResolvePaths(manifest.PackageDir, layer.sources))
        AppendAll(includes, ResolvePaths(manifest.PackageDir, layer.includes))
        AppendAll(includes, ResolvePaths(manifest.PackageDir, layer.private_includes))
    end

    if hasEngineLinked   then AbsorbLayer(layers.native) end
    if hasEngineUnlinked then AbsorbLayer(layers.native_standalone) end

    project(nativeName)
        location(PackageProjectLocation(nativeName))
        kind "SharedLib"
        language "C++"
        cppdialect "C++20"
        cdialect "C17"
        staticruntime "off"
        warnings "Extra"

        targetdir(PackageTargetDirectory())
        objdir(PackageObjectDirectory())

        files(sources)
        includedirs(includes)

        if hasEngineLinked then
            -- The `native` layer links against the engine and picks up its
            -- include set transitively. `native_standalone` skips this — it's
            -- the explicit "no engine link" path.
            UseDependencySet(Dependency.EditorRuntimeCommon)
            if UseFunkeProfilerDependencies then
                UseFunkeProfilerDependencies(true)
            end
            -- Engine is a SharedLib; consumers must declare the import side of FUNKE_API.
            defines { "FNK_IMPORT_DLL" }
            -- Match the engine's EnTT entity-bit split so transitively-included
            -- entt headers see the same entt_traits specialization (otherwise
            -- ODR violation between engine.dll and the package's native lib).
            -- GetFunkeEntityBits() is defined in the root premake5.lua.
            defines { "FUNKE_ENTITY_BITS=" .. tostring(GetFunkeEntityBits()) }
            -- The editor's Rebuild Engine flow rewrites FunkeEntityBitsConfig.h
            -- to override the above -D without a premake regen. The EnTT patch
            -- in entity.hpp picks it up via __has_include. Native package layers
            -- must have the folder on their include path so that override path
            -- stays consistent with the main engine binaries.
            includedirs { FunkeEntityBitsConfigIncludeDir }
            if FunkePackageUseDawnLibDirs ~= false then
                -- Engine-workspace packages link Dawn directly; isolated
                -- project packages only consume the engine import library.
                filter "configurations:Debug"
                    libdirs { path.join(ROOT_DIR, "External/dawn/build/src/dawn/native/Debug") }
                filter "configurations:Release"
                    libdirs { path.join(ROOT_DIR, "External/dawn/build/src/dawn/native/Release") }
                filter "configurations:Dist"
                    libdirs { path.join(ROOT_DIR, "External/dawn/build/src/dawn/native/Release") }
                filter {}
            end
        end

        if manifest.dependencies then
            for _, depName in ipairs(manifest.dependencies) do
                local depNativeName = "Pkg." .. depName .. ".Native"
                links { depNativeName }
                dependson { depNativeName }
            end
        end

        filter "system:windows"
            systemversion "latest"
            -- See Funke-Engine/premake5.lua for the rationale on
            -- MultiProcessorCompile + /Zc:preprocessor.
            flags { "MultiProcessorCompile" }
            buildoptions { "/utf-8", "/FS", "/Zc:preprocessor" }
            defines { "FNK_PLATFORM_WINDOWS" }
            postbuildcommands { NativeAbiMetadataCommand(hasEngineLinked) }

        filter "system:linux"
            pic "On"
            defines { "FNK_PLATFORM_LINUX" }
            postbuildcommands { PosixNativeAbiMetadataCommand(hasEngineLinked) }

        filter "system:macosx"
            defines { "FNK_PLATFORM_MACOS" }
            postbuildcommands { PosixNativeAbiMetadataCommand(hasEngineLinked) }

        filter "configurations:Debug"
            runtime "Debug"
            symbols "On"
            defines { "FNK_DEBUG", "_DEBUG" }

        filter "configurations:Release"
            runtime "Release"
            optimize "On"
            symbols "On"
            defines { "FNK_RELEASE", "NDEBUG" }

        filter "configurations:Dist"
            runtime "Release"
            optimize "Full"
            symbols "Off"
            defines { "FNK_DIST", "NDEBUG" }

        filter {}
end

local function RegisterCSharpProject(manifest)
    local layer = manifest.layers.csharp
    if not layer then
        return
    end

    local csharpName = "Pkg." .. manifest.name

    project(csharpName)
        location(PackageProjectLocation(csharpName))
        kind "SharedLib"
        language "C#"
        dotnetframework "net10.0"
        -- IL runs on any CPU; keep the x64 platform label the build tools pass on every host (arm64 included).
        architecture "x64"

        if layer.allow_unsafe then
            clr "Unsafe"
        end

        targetdir(PackageTargetDirectory())
        objdir(PackageObjectDirectory())

        -- Sensible defaults for Funke packages: nullable annotations enabled, dynamic
        -- loading allowed (so the runtime can Assembly.LoadFrom them), output sits
        -- directly under bin/<config>/<Pkg.Name>/ without a net10.0/ sub-folder.
        vsprops {
            AppendTargetFrameworkToOutputPath = "false",
            Nullable = "enable",
            EnableDynamicLoading = "true",
            AllowUnsafeBlocks = layer.allow_unsafe and "true" or "false",
        }

        files(ResolvePaths(manifest.PackageDir, layer.sources))

        -- Optional NuGet PackageReferences. Premake5's `nuget` directive emits
        -- the corresponding <PackageReference> entries on `dotnet restore`. The
        -- package opts into the dependency; consumers of the package transit-
        -- ively pick it up via the generated assembly. Keep this list short —
        -- adding NuGet refs widens the engine's runtime surface.
        if layer.nuget and #layer.nuget > 0 then
            nuget(layer.nuget)
        end

        -- Funke-ScriptCore is the default reference for every csharp
        -- package: it provides the managed `Component`, `Entity`, `Texture`,
        -- `Vector2Int`, math types, etc. that any package wanting to bind
        -- to live scene state must subclass / construct. Pure-managed
        -- packages that don't touch scene state (e.g. Pkg.Funke.Serialization)
        -- pay nothing for the unused reference. Keeping this implicit means
        -- package authors don't have to plumb the dependency themselves.
        if FunkePackageScriptCoreAssemblies then
            for configuration, assemblyPath in pairs(FunkePackageScriptCoreAssemblies) do
                filter("configurations:" .. configuration)
                    links { assemblyPath }
            end
            filter {}
        else
            links { "Funke-ScriptCore" }
            dependson { "Funke-ScriptCore" }
        end

        -- If this package bridges to its own native sibling via P/Invoke, make
        -- the C# project build-depend on Pkg.<Name>.Native so the native DLL the
        -- resolver loads exists before/with this assembly. Without it a fresh
        -- parallel build can finish the C# DLL first and a run-immediately flow
        -- hits DllNotFound. No `links` — C# P/Invokes at runtime, never links C++.
        if layer.pinvoke_dll then
            dependson { "Pkg." .. manifest.name .. ".Native" }
        end

        if manifest.dependencies then
            for _, depName in ipairs(manifest.dependencies) do
                local depCsharpName = "Pkg." .. depName
                links { depCsharpName }
                dependson { depCsharpName }
            end
        end

        filter "configurations:Debug"
            symbols "On"
            optimize "Off"
            defines { "FNK_DEBUG" }
        filter "configurations:Release"
            optimize "On"
            symbols "On"
            defines { "FNK_RELEASE" }
        filter "configurations:Dist"
            optimize "Full"
            symbols "Off"
            defines { "FNK_DIST" }
        filter {}
end

-- Reads <project>/project.json and extracts the top-level "packages" array.
-- Returns:
--   nil               — no project given (engine-developer mode, legacy scan-all)
--   {}                — project loaded, no packages installed (fresh/empty project)
--   { "A", "B", ... } — explicit allow-list
--
-- Uses pattern matching rather than a full JSON parser because the schema we generate
-- is well-formed and the field shape is trivial (top-level array of strings).
local function ReadProjectPackagesAllowList(projectRootDir)
    if not projectRootDir or projectRootDir == "" then
        return nil
    end

    local manifestPath = path.join(projectRootDir, "project.json")
    if not os.isfile(manifestPath) then
        -- A project path was specified but project.json is missing — fail closed
        -- (treat as no packages installed) rather than scanning everything.
        return {}
    end

    local file = io.open(manifestPath, "rb")
    if not file then
        return {}
    end
    local jsonText = file:read("*all")
    file:close()
    if not jsonText then
        return {}
    end

    -- Strip line comments and block comments (defensive — JSON doesn't have them but
    -- some users add them). Cheap to do.
    jsonText = jsonText:gsub("//[^\n]*", ""):gsub("/%*.-%*/", "")

    -- Locate "packages" : [ ... ]. The string up to the first matching ] is the array.
    local arrayBody = jsonText:match('"packages"%s*:%s*%[(.-)%]')
    if not arrayBody then
        -- Field absent in project.json = no packages installed.
        return {}
    end

    local packages = {}
    -- Pull every double-quoted string out of the array body. Package names contain only
    -- A-Z, a-z, 0-9, '.', '_', '-' so no escape handling needed.
    for name in arrayBody:gmatch('"([^"]*)"') do
        if name ~= "" then
            table.insert(packages, name)
        end
    end

    return packages
end

local function FilterManifestsByAllowList(manifests, byName, allowList)
    local allowed = {}
    for _, name in ipairs(allowList) do
        allowed[name] = true
    end

    -- A user listing package "B" that depends on "A" almost always intends to
    -- pull "A" along too. Auto-include transitive deps before filtering so
    -- TopoSort doesn't later raise "B depends on missing package A" while
    -- A is actually sitting on disk, just filtered out.
    local function MarkTransitive(name, seen)
        if seen[name] then return end
        seen[name] = true
        local manifest = byName[name]
        if not manifest or not manifest.dependencies then return end
        for _, depName in ipairs(manifest.dependencies) do
            allowed[depName] = true
            MarkTransitive(depName, seen)
        end
    end
    do
        local seen = {}
        for _, name in ipairs(allowList) do
            MarkTransitive(name, seen)
        end
    end

    local filtered = {}
    local filteredByName = {}
    local skipped = {}

    for _, manifest in ipairs(manifests) do
        if allowed[manifest.name] then
            table.insert(filtered, manifest)
            filteredByName[manifest.name] = manifest
        else
            table.insert(skipped, manifest.name)
        end
    end

    -- Detect missing dependencies — the user listed a package that isn't on disk anywhere.
    for _, name in ipairs(allowList) do
        if not filteredByName[name] then
            print("[Funke Packages] WARNING: project lists package '" .. name ..
                "' but no manifest with that name was found under packages/ or <project>/Packages/.")
        end
    end

    if #skipped > 0 then
        print("[Funke Packages] Skipped " .. tostring(#skipped) ..
            " package(s) not in the project's allow-list: " .. table.concat(skipped, ", "))
    end

    return filtered, filteredByName
end

-- Registers engine-shipped packages (under <repo>/packages/) into the current
-- premake workspace. Used by the engine's top-level premake5.lua.
--
-- Project-local packages (under <project>/Packages/) are NOT touched here —
-- they belong to a per-project package workspace that lives under
-- <project>/.funke/build/ and is generated by premake/project-packages.lua.
-- Keeping them out of the engine workspace means:
--   * Funke.sln stays static across project switches (no churn when an engine
--     dev opens a project).
--   * Shipped Editor/Launcher binaries that don't have premake5.lua can still
--     build project-local packages — they only need the SDK-side premake
--     script and the bundled EngineSDK.
--
-- The engine workspace no longer loads project-local packages from `--funke-project`;
-- that option only selects entityBits there. This script owns package generation.
function PackageSystem.LoadEnginePackages()
    local manifests = {}
    local byName = {}

    LoadManifestsFromRoot(path.join(ROOT_DIR, "packages"), manifests, byName)

    if #manifests == 0 then
        return
    end

    local sorted = TopoSort(manifests, byName)

    group "Packages"
    for _, manifest in ipairs(sorted) do
        RegisterNativeProject(manifest)
        RegisterCSharpProject(manifest)
    end
    group ""

    local names = {}
    for _, m in ipairs(sorted) do
        table.insert(names, m.name)
    end
    print("[Funke Packages] Registered " .. tostring(#sorted) .. " engine package(s): " .. table.concat(names, ", "))
end

-- Validates and orders a project's enabled packages (dependencies first) without registering
-- any premake project. Raises "[Funke Packages] ..." errors like the loader does.
function PackageSystem.ResolveProjectPackages(projectRootDir)
    if not projectRootDir or projectRootDir == "" then
        PackageError("LoadProjectPackages requires an absolute project root.")
    end

    local manifests = {}
    local byName = {}
    LoadManifestsFromRoot(path.join(ROOT_DIR, "packages"), manifests, byName)
    LoadManifestsFromRoot(path.join(projectRootDir, "Packages"), manifests, byName)

    local allowList = ReadProjectPackagesAllowList(projectRootDir)
    manifests, byName = FilterManifestsByAllowList(manifests, byName, allowList or {})
    if #manifests == 0 then
        return {}
    end
    return TopoSort(manifests, byName)
end

function PackageSystem.LoadProjectPackages(projectRootDir)
    local sorted = PackageSystem.ResolveProjectPackages(projectRootDir)
    if #sorted == 0 then
        print("[Funke Packages] No enabled packages to register for " .. projectRootDir)
        return 0
    end

    group "Packages"
    for _, manifest in ipairs(sorted) do
        RegisterNativeProject(manifest)
        RegisterCSharpProject(manifest)
    end
    group ""

    local names = {}
    for _, manifest in ipairs(sorted) do
        table.insert(names, manifest.name)
    end
    print("[Funke Packages] Registered " .. tostring(#sorted) ..
        " project package(s): " .. table.concat(names, ", "))
    return #sorted
end

-- Backwards-compat alias. Old callers still work — there's a separate
-- project-packages premake script that handles the project-local subset.
PackageSystem.LoadAll = PackageSystem.LoadEnginePackages

return PackageSystem
