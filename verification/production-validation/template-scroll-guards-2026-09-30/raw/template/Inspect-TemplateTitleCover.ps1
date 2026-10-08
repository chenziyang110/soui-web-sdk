param(
  [Parameter(Mandatory=$true)][int]$TargetProcessId,
  [Parameter(Mandatory=$true)][long]$ExpectedHwnd,
  [Parameter(Mandatory=$true)][string]$OutputJson
)
$ErrorActionPreference = "Stop"
if (Test-Path -LiteralPath $OutputJson) { throw "Output exists" }
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public struct TitlePoint { public int X; public int Y; public TitlePoint(int x, int y) { X=x; Y=y; } }
public struct TitleRect { public int Left; public int Top; public int Right; public int Bottom; }
public static class TitleNative {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(TitlePoint point);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out TitleRect rect);
}
"@
$target = [IntPtr]::new($ExpectedHwnd)
[uint32]$owner = 0
[void][TitleNative]::GetWindowThreadProcessId($target, [ref]$owner)
if ($owner -ne $TargetProcessId) { throw "Target HWND owner changed" }
$rect = [TitleRect]::new()
if (-not [TitleNative]::GetWindowRect($target, [ref]$rect)) { throw "Target rectangle unavailable" }
$x = [int]($rect.Left + [math]::Floor(($rect.Right - $rect.Left) / 2))
$y = [int]($rect.Top + 12)
$hit = [TitleNative]::WindowFromPoint([TitlePoint]::new($x,$y))
$root = if ($hit -eq [IntPtr]::Zero) { [IntPtr]::Zero } else { [TitleNative]::GetAncestor($hit, 2) }
$foreground = [TitleNative]::GetForegroundWindow()
[uint32]$hitOwner = 0
[uint32]$foregroundOwner = 0
if ($root -ne [IntPtr]::Zero) { [void][TitleNative]::GetWindowThreadProcessId($root, [ref]$hitOwner) }
if ($foreground -ne [IntPtr]::Zero) { [void][TitleNative]::GetWindowThreadProcessId($foreground, [ref]$foregroundOwner) }
$result = [ordered]@{
  schema = "soui.template-title-cover/v1"
  targetProcessId = $TargetProcessId
  targetHwnd = $ExpectedHwnd
  targetRect = @($rect.Left,$rect.Top,$rect.Right,$rect.Bottom)
  titlePoint = @($x,$y)
  hitHwnd = $hit.ToInt64()
  hitRootHwnd = $root.ToInt64()
  hitRootProcessId = [int]$hitOwner
  foregroundHwnd = $foreground.ToInt64()
  foregroundProcessId = [int]$foregroundOwner
  pointBelongsToTarget = ($root -eq $target)
}
$bytes = [Text.UTF8Encoding]::new($false).GetBytes(($result | ConvertTo-Json -Depth 3) + [Environment]::NewLine)
$stream = [IO.File]::Open($OutputJson,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
$result | ConvertTo-Json -Depth 3 -Compress