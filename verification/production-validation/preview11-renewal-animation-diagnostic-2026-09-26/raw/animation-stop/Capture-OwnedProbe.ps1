param([Parameter(Mandatory=$true)][int]$ProbePid,[Parameter(Mandatory=$true)][string]$ExpectedExecutable,[Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputFile){throw 'Refuse existing capture'}
$probe=Get-Process -Id $ProbePid -ErrorAction Stop
if([IO.Path]::GetFullPath($probe.Path) -ne [IO.Path]::GetFullPath($ExpectedExecutable)){throw 'Probe executable identity mismatch'}
$probe.Refresh(); $hwnd=$probe.MainWindowHandle
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ProbeWindowCapture {
 public delegate bool EnumProc(IntPtr hwnd,IntPtr param);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback,IntPtr param);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd,System.Text.StringBuilder text,int count);
 public static IntPtr Find(uint target) {IntPtr found=IntPtr.Zero;EnumWindows((hwnd,p)=>{uint owner;GetWindowThreadProcessId(hwnd,out owner);var title=new System.Text.StringBuilder(256);GetWindowText(hwnd,title,256);if(owner==target && title.ToString()=="SOUI SDK same-document renewal validation"){found=hwnd;return false;}return true;},IntPtr.Zero);return found;}
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd,out Rect rect);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd,IntPtr hdc,uint flags);
}
'@
if($hwnd -eq [IntPtr]::Zero){$hwnd=[ProbeWindowCapture]::Find([uint32]$ProbePid)}
if($hwnd -eq [IntPtr]::Zero){throw 'Probe has no window'}
[uint32]$owner=0
[void][ProbeWindowCapture]::GetWindowThreadProcessId($hwnd,[ref]$owner)
if($owner -ne $ProbePid){throw 'Window owner changed'}
$rect=New-Object ProbeWindowCapture+Rect
if(-not [ProbeWindowCapture]::GetWindowRect($hwnd,[ref]$rect)){throw 'No probe bounds'}
$width=$rect.Right-$rect.Left;$height=$rect.Bottom-$rect.Top
if($width -lt 1 -or $height -lt 1 -or $width -gt 4096 -or $height -gt 4096){throw 'Invalid probe bounds'}
$bitmap=New-Object Drawing.Bitmap $width,$height
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$dc=$graphics.GetHdc()
try {$captured=[ProbeWindowCapture]::PrintWindow($hwnd,$dc,2)} finally {$graphics.ReleaseHdc($dc);$graphics.Dispose()}
try {if(-not $captured){throw 'PrintWindow failed'}; $bitmap.Save($OutputFile,[Drawing.Imaging.ImageFormat]::Png)} finally {$bitmap.Dispose()}
[ordered]@{pid=$ProbePid;program=$ExpectedExecutable;capturedAt=[DateTime]::UtcNow.ToString('o');method='PrintWindow, only HWND belonging to the exact test executable';width=$width;height=$height;output=$OutputFile}|ConvertTo-Json|Set-Content -LiteralPath ($OutputFile+'.json') -Encoding utf8
