param(
    [Parameter(Mandatory=$true)][int]$TargetProcessId,
    [Parameter(Mandatory=$true)][UInt64]$ExpectedCreationFiletime,
    [Parameter(Mandatory=$true)][UInt64]$ExpectedHwnd,
    [Parameter(Mandatory=$true)][string]$OutputJson
)

$ErrorActionPreference = 'Stop'
if ($TargetProcessId -le 0 -or $ExpectedCreationFiletime -eq 0 -or $ExpectedHwnd -eq 0) {
    throw 'Target PID, creation identity, and HWND must be nonzero'
}
if (-not [IO.Path]::IsPathRooted($OutputJson) -or
    -not $OutputJson.StartsWith('C:\', [StringComparison]::OrdinalIgnoreCase) -or
    [IO.File]::Exists($OutputJson) -or [IO.Directory]::Exists($OutputJson) -or
    -not [IO.Directory]::Exists([IO.Path]::GetDirectoryName($OutputJson))) {
    throw 'Result must use a new absolute C: file in an existing directory'
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class V02WindowCloseNative {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessageW(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] public static extern bool GetProcessTimes(IntPtr process, out long creation, out long exit, out long kernel, out long user);
    [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr handle, out uint code);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@

$window = [IntPtr][int64]$ExpectedHwnd
$process = [V02WindowCloseNative]::OpenProcess([uint32]0x101000, $false, [uint32]$TargetProcessId)
if ($process -eq [IntPtr]::Zero) { throw 'Target process unavailable' }
$passed = $false
try {
    $creation = [long]0; $exit = [long]0; $kernel = [long]0; $user = [long]0
    if (-not [V02WindowCloseNative]::GetProcessTimes($process, [ref]$creation, [ref]$exit, [ref]$kernel, [ref]$user) -or
        [uint64]$creation -ne $ExpectedCreationFiletime) {
        throw 'Target process creation identity differs'
    }
    $owner = [uint32]0
    [void][V02WindowCloseNative]::GetWindowThreadProcessId($window, [ref]$owner)
    if (-not [V02WindowCloseNative]::IsWindow($window) -or
        -not [V02WindowCloseNative]::IsWindowVisible($window) -or
        $owner -ne [uint32]$TargetProcessId -or
        [V02WindowCloseNative]::GetForegroundWindow() -ne $window) {
        throw 'Target window is not the visible natural foreground window'
    }

    $clock = [Diagnostics.Stopwatch]::StartNew()
    $posted = [V02WindowCloseNative]::PostMessageW($window, [uint32]0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    $postError = if ($posted) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
    $waitResult = if ($posted) { [V02WindowCloseNative]::WaitForSingleObject($process, [uint32]45000) } else { [uint32]0xffffffff }
    $clock.Stop()
    $exitCode = [uint32]0
    $exitCodeAvailable = [V02WindowCloseNative]::GetExitCodeProcess($process, [ref]$exitCode)
    $record = [ordered]@{
        schema = 'soui.v02.native-close/v1'
        at = [DateTime]::UtcNow.ToString('o')
        pid = $TargetProcessId
        creationFiletime = $ExpectedCreationFiletime.ToString()
        hwnd = $ExpectedHwnd.ToString()
        naturallyForegroundBeforeClose = $true
        message = 'WM_CLOSE'
        posted = $posted
        postError = $postError
        waitResult = $waitResult
        elapsedMs = $clock.ElapsedMilliseconds
        processExited = ($waitResult -eq 0)
        windowDestroyed = (-not [V02WindowCloseNative]::IsWindow($window))
        nativeExitCode = if ($exitCodeAvailable -and $waitResult -eq 0) { $exitCode } else { $null }
        forcedTermination = $false
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
    $passed = $posted -and $waitResult -eq 0 -and $exitCode -eq 0 -and $record.windowDestroyed
} finally {
    [void][V02WindowCloseNative]::CloseHandle($process)
}
if (-not $passed) { exit 1 }
