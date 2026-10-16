# Explicit, filesystem-only discovery for the proposed LLVM22MSVC142 profile.
# No process, PATH search, registry lookup, environment mutation, or fallback.

function Resolve-SouiLlvmPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][ValidateSet('Directory', 'File')][string]$Kind
    )
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'LLVM22MSVC142 discovery requires Windows.' }
    if ($Path -notmatch '^[A-Za-z]:[\\/]' -or $Path -match '[;"\x00-\x1f]') {
        throw "LLVM profile requires an absolute local path without CMake list or control characters: $Path"
    }
    $full = [IO.Path]::GetFullPath($Path)
    $item = Get-Item -LiteralPath $full -Force -ErrorAction Stop
    if (($Kind -eq 'Directory') -ne [bool]$item.PSIsContainer) {
        throw "LLVM profile expected $Kind at $full"
    }
    $cursor = $item
    while ($null -ne $cursor) {
        if (($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "LLVM profile refuses a reparse point: $($cursor.FullName)"
        }
        $parent = [IO.Path]::GetDirectoryName($cursor.FullName.TrimEnd([char[]]'\/'))
        if ([string]::IsNullOrEmpty($parent)) { break }
        $cursor = Get-Item -LiteralPath $parent -Force -ErrorAction Stop
        if (-not $cursor.PSIsContainer) { throw "LLVM profile ancestor is not a directory: $parent" }
    }
    $canonical = $item.FullName.Replace('\', '/')
    if ($canonical.Length -gt 3) { $canonical = $canonical.TrimEnd('/') }
    return $canonical
}

function Get-SouiLlvmFileIdentity {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path, [switch]$HostExecutable)
    $full = Resolve-SouiLlvmPath -Path $Path -Kind File
    $item = Get-Item -LiteralPath $full -Force -ErrorAction Stop
    $machine = $null
    if ($HostExecutable) {
        # Read fixed headers only. This establishes host PE architecture, not runtime compatibility.
        $stream = [IO.File]::Open($full, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        try {
            $reader = [IO.BinaryReader]::new($stream)
            if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw "Not a DOS/PE executable: $full" }
            $stream.Position = 60
            $peOffset = $reader.ReadUInt32()
            if ($peOffset -lt 64 -or [long]$peOffset -gt $stream.Length - 26) { throw "Invalid PE header extent: $full" }
            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $full" }
            $machine = $reader.ReadUInt16()
            $stream.Position = [long]$peOffset + 20
            $optionalSize = $reader.ReadUInt16()
            $characteristics = $reader.ReadUInt16()
            if ($optionalSize -lt 2 -or [long]$peOffset + 24 + $optionalSize -gt $stream.Length) {
                throw "Invalid PE optional-header extent: $full"
            }
            if ($machine -ne 0x8664 -or $reader.ReadUInt16() -ne 0x20b -or ($characteristics -band 2) -eq 0) {
                throw "LLVM profile requires an AMD64 PE32+ host executable: $full"
            }
        } finally { $stream.Dispose() }
    }
    return [ordered]@{
        path = $full
        bytes = [long]$item.Length
        sha256 = (Get-FileHash -LiteralPath $full -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
        hostMachine = $machine
        # May be empty or '0'. Never substitute the requested LLVM package version.
        resourceFileVersion = if ($HostExecutable) { [Diagnostics.FileVersionInfo]::GetVersionInfo($full).FileVersion } else { $null }
    }
}

function Get-SouiLlvmToolchain {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][ValidateSet('x86', 'x64')][string]$Architecture,
        [Parameter(Mandatory)][string]$LlvmRoot,
        [Parameter(Mandatory)][string]$MsvcRoot,
        [Parameter(Mandatory)][string]$WindowsSdkRoot,
        [Parameter(Mandatory)][string]$NinjaExe,
        [Parameter(Mandatory)][string]$WindowsCmdExe,
        [Parameter(Mandatory)][string]$PowerShellExe,
        [Parameter(Mandatory)][string]$ToolIdentityBridge,
        [Parameter(Mandatory)][string]$ToolIdentityHelper
    )
    $llvm = Resolve-SouiLlvmPath -Path $LlvmRoot -Kind Directory
    $msvc = Resolve-SouiLlvmPath -Path $MsvcRoot -Kind Directory
    $sdk = Resolve-SouiLlvmPath -Path $WindowsSdkRoot -Kind Directory
    $ninja = Resolve-SouiLlvmPath -Path $NinjaExe -Kind File
    $cmd = Resolve-SouiLlvmPath -Path $WindowsCmdExe -Kind File
    $powershell = Resolve-SouiLlvmPath -Path $PowerShellExe -Kind File
    $bridge = Resolve-SouiLlvmPath -Path $ToolIdentityBridge -Kind File
    $helper = Resolve-SouiLlvmPath -Path $ToolIdentityHelper -Kind File
    if ([IO.Path]::GetFileName($bridge) -cne 'Invoke-ToolFileIdentity.ps1' -or
            $helper -cne ([IO.Path]::GetDirectoryName($bridge).Replace('\', '/') + '/Tool-FileIdentity.ps1')) {
        throw 'The explicit identity bridge and helper must use their fixed sibling layout.'
    }
    if ([IO.Path]::GetFileName($msvc) -cne '14.29.30133') {
        throw 'LLVM22MSVC142 requires the explicit MSVC 14.29.30133 installation root.'
    }
    $sdkVersion = '10.0.19041.0'
    $target = if ($Architecture -eq 'x64') { 'x86_64-pc-windows-msvc' } else { 'i686-pc-windows-msvc' }
    $includeDirectories = @(
        ($llvm + '/lib/clang/22/include'), ($msvc + '/include'),
        ($sdk + '/Include/' + $sdkVersion + '/ucrt'),
        ($sdk + '/Include/' + $sdkVersion + '/shared'),
        ($sdk + '/Include/' + $sdkVersion + '/um'),
        ($sdk + '/Include/' + $sdkVersion + '/winrt')
    )
    $libraryDirectories = @(
        ($msvc + '/lib/' + $Architecture),
        ($sdk + '/Lib/' + $sdkVersion + '/ucrt/' + $Architecture),
        ($sdk + '/Lib/' + $sdkVersion + '/um/' + $Architecture)
    )
    foreach ($directory in @($includeDirectories) + @($libraryDirectories)) {
        [void](Resolve-SouiLlvmPath -Path $directory -Kind Directory)
    }
    $roles = [ordered]@{
        CLANG = ($llvm + '/bin/clang-cl.exe')
        LINKER = ($llvm + '/bin/lld-link.exe')
        ARCHIVER = ($llvm + '/bin/llvm-lib.exe')
        RC = ($sdk + '/bin/' + $sdkVersion + '/x64/rc.exe')
        MT = ($sdk + '/bin/' + $sdkVersion + '/x64/mt.exe')
        NINJA = $ninja
        CMD = $cmd
        POWERSHELL = $powershell
    }
    $sentinels = @(
        ($includeDirectories[0] + '/stddef.h'), ($includeDirectories[1] + '/vector'),
        ($includeDirectories[1] + '/optional'), ($includeDirectories[1] + '/vcruntime.h'),
        ($includeDirectories[2] + '/stdlib.h'), ($includeDirectories[3] + '/sdkddkver.h'),
        ($includeDirectories[4] + '/Windows.h'), ($includeDirectories[5] + '/EventToken.h'),
        ($libraryDirectories[0] + '/msvcrt.lib'), ($libraryDirectories[0] + '/msvcprt.lib'),
        ($libraryDirectories[0] + '/vcruntime.lib'), ($libraryDirectories[0] + '/libcmt.lib'),
        ($libraryDirectories[0] + '/libcpmt.lib'), ($libraryDirectories[0] + '/libvcruntime.lib'),
        ($libraryDirectories[1] + '/ucrt.lib'), ($libraryDirectories[1] + '/libucrt.lib'),
        ($libraryDirectories[2] + '/kernel32.lib'), ($libraryDirectories[2] + '/user32.lib')
    )
    $toolPins = [ordered]@{}
    $inputPins = [Collections.Generic.List[object]]::new()
    $bridgePins = @(Get-SouiLlvmFileIdentity -Path $bridge; Get-SouiLlvmFileIdentity -Path $helper)
    $powershellPin = Get-SouiLlvmFileIdentity -Path $powershell -HostExecutable
    $bridgeLines = @('soui.llvm-tool-bridge-inputs/v1', $powershell, $bridge, $helper)
    foreach ($pin in @($powershellPin) + $bridgePins) {
        $bridgeLines += $pin.path + '|' + $pin.bytes.ToString([Globalization.CultureInfo]::InvariantCulture) + '|' + $pin.sha256
    }
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $bridgeDigest = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes([string]::Join("`n", $bridgeLines)))).Replace('-', '').ToLowerInvariant() }
    finally { $hasher.Dispose() }
    $identityLines = [Collections.Generic.List[string]]::new()
    foreach ($value in @('soui.llvm-profile-inputs/v3', 'LLVM22MSVC142', '22.1.3', '19.29.30159', $sdkVersion,
            $Architecture, $target, $llvm, $msvc, $sdk, $ninja, $cmd, $powershell, $bridge, $helper, $bridgeDigest)) { $identityLines.Add($value) }
    foreach ($role in $roles.Keys) {
        $pin = Get-SouiLlvmFileIdentity -Path $roles[$role] -HostExecutable
        $toolPins[$role] = $pin
        $identityLines.Add($pin.path + '|' + $pin.bytes.ToString([Globalization.CultureInfo]::InvariantCulture) + '|' + $pin.sha256)
    }
    foreach ($path in $sentinels) {
        $pin = Get-SouiLlvmFileIdentity -Path $path
        $inputPins.Add($pin)
        $identityLines.Add($pin.path + '|' + $pin.bytes.ToString([Globalization.CultureInfo]::InvariantCulture) + '|' + $pin.sha256)
    }
    foreach ($pin in $bridgePins) {
        $identityLines.Add($pin.path + '|' + $pin.bytes.ToString([Globalization.CultureInfo]::InvariantCulture) + '|' + $pin.sha256)
    }
    $identityBytes = [Text.Encoding]::UTF8.GetBytes([string]::Join("`n", $identityLines))
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($hasher.ComputeHash($identityBytes)).Replace('-', '').ToLowerInvariant() }
    finally { $hasher.Dispose() }
    $definitions = [ordered]@{
        SOUI_NATIVE_TOOLCHAIN = 'LLVM22MSVC142'
        SOUI_LLVM_ARCHITECTURE = $Architecture
        SOUI_LLVM_ROOT = $llvm
        SOUI_MSVC142_ROOT = $msvc
        SOUI_WINDOWS_SDK_ROOT = $sdk
        SOUI_NINJA_EXECUTABLE = $ninja
        SOUI_WINDOWS_CMD_EXECUTABLE = $cmd
        SOUI_LLVM_INPUT_IDENTITY = $digest
        SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE = $powershell
        SOUI_TOOL_IDENTITY_BRIDGE = $bridge
        SOUI_TOOL_IDENTITY_HELPER = $helper
        SOUI_LLVM_BRIDGE_IDENTITY = $bridgeDigest
    }
    return [ordered]@{
        Profile = 'LLVM22MSVC142'
        Architecture = $Architecture
        HostArchitecture = 'x64'
        RequestedClangVersion = '22.1.3'
        ActualCompilerVersion = $null # CMake must discover and verify this after enabling languages.
        MsvcVersion = '14.29.30133'
        MsvcCompatibilityVersion = '19.29.30159'
        WindowsSdkVersion = $sdkVersion
        Target = $target
        Generator = 'Ninja Multi-Config'
        GeneratorValidated = $false
        ToolPins = $toolPins
        # Use this native spelling only for Windows process launch / ComSpec.
        # The calling guard owns presence/value save, scoped assignment, and restoration.
        NativeProcessPaths = [ordered]@{ CMD = [IO.Path]::GetFullPath($cmd) }
        RequiredInputPins = $inputPins.ToArray()
        BridgePins = $bridgePins
        BridgeIdentity = $bridgeDigest
        InputIdentitySchema = 'soui.llvm-profile-inputs/v3'
        IncludeDirectories = $includeDirectories
        LibraryDirectories = $libraryDirectories
        CMakeDefinitions = $definitions
        InputIdentity = $digest
        WholeToolInventoryVerified = $false
        Qualified = $false
    }
}
