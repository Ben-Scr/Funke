param([Parameter(Mandatory)][string]$AssemblyPath)
$ErrorActionPreference = 'Stop'
$assembly = [Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $AssemblyPath))
$flags = [Reflection.BindingFlags]'Public,Instance,Static,DeclaredOnly'
$nullability = [Reflection.NullabilityInfoContext]::new()

function Format-Type([Type]$Type) {
    if ($Type.IsByRef) { return (Format-Type $Type.GetElementType()) + '&' }
    if ($Type.IsPointer) { return (Format-Type $Type.GetElementType()) + '*' }
    if ($Type.IsArray) { return (Format-Type $Type.GetElementType()) + '[' + (',' * ($Type.GetArrayRank()-1)) + ']' }
    if ($Type.IsGenericParameter) { return $Type.Name }
    if ($Type.IsGenericType) {
        $name = ($Type.GetGenericTypeDefinition().FullName -replace '`\d+', '') -replace '\+', '.'
        $args = @($Type.GetGenericArguments() | ForEach-Object { Format-Type $_ })
        if ($name -eq 'System.Nullable') { return $args[0] + '?' }
        return "$name<" + ($args -join ',') + '>'
    }
    if ($Type.IsFunctionPointer) { return $Type.ToString() }
    return $Type.FullName.Replace('+','.')
}

$rows = foreach ($type in $assembly.GetExportedTypes() | Sort-Object FullName) {
    $members = foreach ($member in $type.GetMembers($flags)) {
        $kind = $member.MemberType.ToString()
        $name = $member.Name
        $parameters = @()
        $typeName = $null
        $access = ''
        $generic = @()
        switch ($kind) {
            'Method' {
                if ($member.IsSpecialName -and !$name.StartsWith('op_')) { continue }
                $typeName = Format-Type $member.ReturnType
                $parameters = $member.GetParameters()
                $static = $member.IsStatic
                if ($member.IsGenericMethod) { $generic = @($member.GetGenericArguments() | ForEach-Object Name) }
            }
            'Constructor' { $parameters = $member.GetParameters(); $static = $false }
            'Property' {
                $typeName = Format-Type $member.PropertyType
                $parameters = $member.GetIndexParameters()
                $static = $member.GetAccessors($false)[0].IsStatic
                if ($member.GetGetMethod($false)) { $access += 'g' }
                if ($member.GetSetMethod($false)) { $access += 's' }
            }
            'Event' { $static = $member.GetAddMethod().IsStatic; $typeName = Format-Type $member.EventHandlerType }
            'Field' { $static = $member.IsStatic; $typeName = Format-Type $member.FieldType }
        }
        if ($kind -eq 'NestedType' -or $name -eq 'value__' -or
            ($kind -eq 'Method' -and $member.IsSpecialName -and !$name.StartsWith('op_'))) { continue }
        $readOnly = $kind -eq 'Field' -and ($member.IsInitOnly -or $member.IsLiteral)
        $arguments = @(
            foreach ($parameter in $parameters) {
                $passing = if ($parameter.IsOut) { 'out' } elseif ($parameter.IsIn) { 'in' } elseif ($parameter.ParameterType.IsByRef) { 'ref' } else { '' }
                [ordered]@{
                    name = $parameter.Name
                    type = Format-Type $parameter.ParameterType
                    nullable = $nullability.Create($parameter).ReadState.ToString()
                    passing = $passing
                    variadic = $parameter.IsDefined([ParamArrayAttribute], $false)
                    optional = $parameter.IsOptional
                    default = if ($parameter.HasDefaultValue) { $parameter.DefaultValue }
                }
            }
        )
        [ordered]@{ arguments=$arguments; readOnly=$readOnly; name=$name; kind=$kind; static=$static; result=$typeName; access=$access; generic=$generic;
            parameters=@($parameters | ForEach-Object { Format-Type $_.ParameterType }); signature=$member.ToString() }
    }
    [ordered]@{ type=($type.FullName -replace '\+', '.'); generic=@($type.GetGenericArguments() | ForEach-Object Name); members=@($members) }
}
ConvertTo-Json -InputObject @($rows) -Depth 6 -Compress
