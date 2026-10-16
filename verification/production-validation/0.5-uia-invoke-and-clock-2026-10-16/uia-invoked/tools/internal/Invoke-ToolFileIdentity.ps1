[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ObservedPath,
    [Parameter(Mandatory)][string]$ExpectedPath,
    [Parameter(Mandatory)][long]$ExpectedBytes,
    [Parameter(Mandatory)][string]$ExpectedSha256,
    [Parameter(Mandatory)][string]$HelperPath,
    [Parameter(Mandatory)][long]$HelperBytes,
    [Parameter(Mandatory)][string]$HelperSha256
)
$ErrorActionPreference = 'Stop'
$helperStream = $null
try {
    if ($ExpectedBytes -le 0 -or $ExpectedSha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $HelperBytes -le 0 -or $HelperBytes -gt 1048576 -or $HelperSha256 -cnotmatch '^[0-9a-f]{64}$') {
        throw 'Invalid fixed file identity input.'
    }
    foreach ($path in @($ObservedPath, $ExpectedPath, $HelperPath)) {
        if ($path -notmatch '^[A-Za-z]:[\\/]' -or $path -match '[;"\x00-\x1f]') {
            throw 'An absolute local path without list/control characters is required.'
        }
    }
    $helperFull = [IO.Path]::GetFullPath($HelperPath)
    $sibling = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'Tool-FileIdentity.ps1'))
    if ($helperFull -cne $sibling) { throw 'The explicitly bound helper must be the bridge sibling.' }
    $item = Get-Item -LiteralPath $helperFull -Force
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Plain helper file required.'
    }
    $cursor = $item.Directory
    while ($null -ne $cursor) {
        if ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Plain helper ancestors required.' }
        $cursor = $cursor.Parent
    }
    # Keep the checked helper body deny-write/delete shared while dot-sourcing and invoking it.
    $helperStream = [IO.File]::Open($helperFull, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    if ($helperStream.Length -ne $HelperBytes) { throw 'Helper length changed.' }
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $sha = [BitConverter]::ToString($hasher.ComputeHash($helperStream)).Replace('-', '').ToLowerInvariant() }
    finally { $hasher.Dispose() }
    if ($sha -cne $HelperSha256) { throw 'Helper body changed.' }
    . $helperFull
    $receipt = Assert-ToolFileIdentity -ObservedPath $ObservedPath -ExpectedPin ([ordered]@{
        path = $ExpectedPath; bytes = $ExpectedBytes; sha256 = $ExpectedSha256
    })
    if ($receipt.VolumeSerial -cnotmatch '^[0-9a-f]{8}$' -or $receipt.FileIndex -cnotmatch '^[0-9a-f]{16}$' -or
            $receipt.Bytes -ne $ExpectedBytes -or $receipt.Sha256 -cne $ExpectedSha256) {
        throw 'Unexpected identity receipt.'
    }
    [Console]::Out.WriteLine(('SOUI_LLVM_TOOL_FILE_IDENTITY_V1|{0}|{1}|{2}|{3}' -f
        $ExpectedSha256, $ExpectedBytes.ToString([Globalization.CultureInfo]::InvariantCulture),
        $receipt.VolumeSerial, $receipt.FileIndex))
} catch {
    $message = $_.Exception.Message
    if ($message.Length -gt 512) { $message = $message.Substring(0, 512) }
    [Console]::Error.WriteLine('E_LLVM_TOOL_FILE_IDENTITY: ' + $message)
    exit 1
} finally {
    if ($null -ne $helperStream) { $helperStream.Dispose() }
}
