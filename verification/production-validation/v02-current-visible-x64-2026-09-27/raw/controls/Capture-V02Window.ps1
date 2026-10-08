param(
    [Parameter(Mandatory=$true)][int]$TargetProcessId,
    [Parameter(Mandatory=$true)][UInt64]$ExpectedCreationFiletime,
    [Parameter(Mandatory=$true)][UInt64]$ExpectedHwnd,
    [Parameter(Mandatory=$true)][string]$OutputPng,
    [Parameter(Mandatory=$true)][string]$OutputJson
)

$ErrorActionPreference = 'Stop'
if ([StringComparer]::OrdinalIgnoreCase.Equals(
    [IO.Path]::GetFullPath($OutputPng), [IO.Path]::GetFullPath($OutputJson))) {
    throw 'Image and metadata paths must differ'
}
foreach ($path in @($OutputPng, $OutputJson)) {
    if (-not [IO.Path]::IsPathRooted($path) -or -not $path.StartsWith('C:\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Each result must use a new absolute C: path'
    }
    if ([IO.File]::Exists($path) -or [IO.Directory]::Exists($path)) {
        throw "Result already exists: $path"
    }
    if (-not [IO.Directory]::Exists([IO.Path]::GetDirectoryName($path))) {
        throw "Result directory does not exist: $path"
    }
}
if ($TargetProcessId -le 0 -or $ExpectedCreationFiletime -eq 0 -or $ExpectedHwnd -eq 0) {
    throw 'Target PID, creation identity, and HWND must be nonzero'
}

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class V02WindowCaptureNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] public static extern bool GetProcessTimes(IntPtr process, out long creation, out long exit, out long kernel, out long user);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@

$window = [IntPtr][int64]$ExpectedHwnd
$process = [V02WindowCaptureNative]::OpenProcess([uint32]0x1000, $false, [uint32]$TargetProcessId)
if ($process -eq [IntPtr]::Zero) { throw 'Target process unavailable' }
try {
    $creation = [long]0; $exit = [long]0; $kernel = [long]0; $user = [long]0
    if (-not [V02WindowCaptureNative]::GetProcessTimes($process, [ref]$creation, [ref]$exit, [ref]$kernel, [ref]$user)) {
        throw 'Target creation identity unavailable'
    }
    if ([uint64]$creation -ne $ExpectedCreationFiletime) { throw 'Target creation identity changed' }
    $owner = [uint32]0
    [void][V02WindowCaptureNative]::GetWindowThreadProcessId($window, [ref]$owner)
    if (-not [V02WindowCaptureNative]::IsWindow($window) -or
        -not [V02WindowCaptureNative]::IsWindowVisible($window) -or
        $owner -ne [uint32]$TargetProcessId -or
        [V02WindowCaptureNative]::GetForegroundWindow() -ne $window) {
        throw 'Target window is not the visible natural foreground window'
    }
    $rect = [V02WindowCaptureNative+Rect]::new()
    if (-not [V02WindowCaptureNative]::GetWindowRect($window, [ref]$rect)) { throw 'Target rectangle unavailable' }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0 -or $width -gt 10000 -or $height -gt 10000) {
        throw 'Target rectangle is invalid'
    }
    $bitmap = [Drawing.Bitmap]::new($width, $height)
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, [Drawing.Size]::new($width, $height))
        } finally { $graphics.Dispose() }
        if ([V02WindowCaptureNative]::GetForegroundWindow() -ne $window) {
            throw 'Foreground changed during screenshot'
        }
        $ownerAfter = [uint32]0
        [void][V02WindowCaptureNative]::GetWindowThreadProcessId($window, [ref]$ownerAfter)
        if (-not [V02WindowCaptureNative]::IsWindow($window) -or
            -not [V02WindowCaptureNative]::IsWindowVisible($window) -or
            $ownerAfter -ne [uint32]$TargetProcessId) {
            throw 'Target window changed during screenshot'
        }
        $after = [V02WindowCaptureNative+Rect]::new()
        if (-not [V02WindowCaptureNative]::GetWindowRect($window, [ref]$after) -or
            $after.Left -ne $rect.Left -or $after.Top -ne $rect.Top -or
            $after.Right -ne $rect.Right -or $after.Bottom -ne $rect.Bottom) {
            throw 'Target rectangle changed during screenshot'
        }
        $file = [IO.FileStream]::new($OutputPng, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
        try {
            $bitmap.Save($file, [Drawing.Imaging.ImageFormat]::Png)
            $file.Flush($true)
        } finally { $file.Dispose() }
    } finally { $bitmap.Dispose() }
    $imageSha = (Get-FileHash -LiteralPath $OutputPng -Algorithm SHA256).Hash.ToLowerInvariant()
    $record = [ordered]@{
        schema = 'soui.v02.target-window-screenshot/v1'
        at = [DateTime]::UtcNow.ToString('o')
        pid = $TargetProcessId
        creationFiletime = $ExpectedCreationFiletime.ToString()
        hwnd = $ExpectedHwnd.ToString()
        naturallyForeground = $true
        rect = @($rect.Left, $rect.Top, $rect.Right, $rect.Bottom)
        imagePath = $OutputPng
        imageBytes = ([IO.FileInfo]::new($OutputPng)).Length
        imageSha256 = $imageSha
        pageStateVerified = $false
        imeCompositionVerified = $false
        releaseReady = $false
    }
    $json = ($record | ConvertTo-Json -Depth 4) + "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json)
    $stream = [IO.FileStream]::new($OutputJson, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
    try {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    } finally { $stream.Dispose() }
    Write-Output $json
} finally {
    [void][V02WindowCaptureNative]::CloseHandle($process)
}
