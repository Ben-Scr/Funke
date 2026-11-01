function Read-PePolicy {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Not a PE image: $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or [long]$peOffset + 96 -gt $bytes.Length) {
        throw "Invalid PE header offset: $Path"
    }
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) {
        throw "Invalid PE signature: $Path"
    }
    $optionalOffset = $peOffset + 24
    $magic = [BitConverter]::ToUInt16($bytes, $optionalOffset)
    if ($magic -notin @(0x10B, 0x20B)) {
        throw "Unsupported PE optional header: $Path"
    }
    return [pscustomobject]@{
        Machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
        Is64Bit = $magic -eq 0x20B
        IsDll = ([BitConverter]::ToUInt16($bytes, $peOffset + 22) -band 0x2000) -ne 0
        Subsystem = [BitConverter]::ToUInt16($bytes, $optionalOffset + 68)
        DllCharacteristics = [BitConverter]::ToUInt16($bytes, $optionalOffset + 70)
    }
}

function Assert-NativePePolicy {
    param([string]$Path)

    $policy = Read-PePolicy $Path
    $expectedDll = [System.IO.Path]::GetExtension($Path) -ieq ".dll"
    # DLLs may use either desktop subsystem; only the executables must hide the console.
    $subsystems = if ($expectedDll) { @(2, 3) } else { @(2) }
    if ($policy.Machine -ne 0x8664 -or -not $policy.Is64Bit -or
        $policy.IsDll -ne $expectedDll -or $policy.Subsystem -notin $subsystems) {
        throw "PE platform/subsystem policy failed: $Path (machine=$($policy.Machine), subsystem=$($policy.Subsystem), dll=$($policy.IsDll))"
    }
    $requiredCharacteristics = [ordered]@{
        HighEntropyVA = 0x0020
        DynamicBase = 0x0040
        NxCompat = 0x0100
        GuardCF = 0x4000
    }
    foreach ($characteristic in $requiredCharacteristics.GetEnumerator()) {
        if (($policy.DllCharacteristics -band $characteristic.Value) -eq 0) {
            throw "PE hardening flag $($characteristic.Key) is missing: $Path"
        }
    }
}
