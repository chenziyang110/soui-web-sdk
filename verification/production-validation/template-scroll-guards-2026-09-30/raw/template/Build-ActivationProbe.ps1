param([Parameter(Mandatory=$true)][ValidateSet("x64","x86")][string]$Architecture)
$ErrorActionPreference = "Stop"
$root = "C:\SOUI-SDK-production-validation\template-scrollbar-preparation-20260929\activation-probe-input-attempt1"
$source = Join-Path $root "V02InputProbe.cpp"
$expected = "788785ffd296190c5a7d67741c4a8c6f15cd788001975ac66de3dc19db2e6437"
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "Source identity changed before build" }
$platform = if ($Architecture -eq "x86") { "Win32" } else { "x64" }
$build = Join-Path $root ("build\" + $Architecture)
& cmake -S $root -B $build -A $platform
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
& cmake --build $build --config Release --target V02InputProbe
if ($LASTEXITCODE -ne 0) { throw "CMake build failed: $LASTEXITCODE" }
$exe = Join-Path $build "Release\V02InputProbe.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "Probe executable missing" }
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "Source identity changed after build" }
[pscustomobject]@{
  architecture = $Architecture
  sourceSha256 = $expected
  executable = $exe
  executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
  executableBytes = (Get-Item -LiteralPath $exe).Length
} | ConvertTo-Json -Compress