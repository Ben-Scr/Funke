local function QuoteVendorPatchArgument(value)
    if os.host() == "windows" then
        return '"' .. value:gsub('"', '\\"') .. '"'
    end
    return "'" .. value:gsub("'", "'\\''") .. "'"
end

function ApplyFunkeVendorPatches()
    local python = os.host() == "windows" and "python" or "python3"
    local script = path.join(ROOT_DIR, "scripts/vendor_patches.py")
    local command = python .. " " .. QuoteVendorPatchArgument(script) .. " apply"
    local exitCode = os.execute(command)
    if exitCode ~= 0 and exitCode ~= true then
        error("[vendor-patches] Failed. See the error above; no project files were generated.")
    end
end
