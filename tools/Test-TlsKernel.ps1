[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$KernelX64,
  [Parameter(Mandatory=$true)][string]$KernelX86,
  [Parameter(Mandatory=$true)][string]$ProbeX64,
  [Parameter(Mandatory=$true)][string]$ProbeX86,
  [Parameter(Mandatory=$true)][string]$Certificate,
  [Parameter(Mandatory=$true)][string]$OutputPath,
  [int]$Port=18443
)
$ErrorActionPreference='Stop'
$cert=(Resolve-Path -LiteralPath $Certificate).Path
$cases=@(
  @{name='untrusted-chain'; host='localhost'; path='/'; trust=$false; disable=$false; expected=1; curlError=60},
  @{name='trusted-matching-host'; host='localhost'; path='/'; trust=$true; disable=$false; expected=0; curlError=$null},
  @{name='trusted-wrong-host'; host='127.0.0.1'; path='/'; trust=$true; disable=$false; expected=1; curlError=51},
  @{name='callback-cannot-disable-chain-check'; host='localhost'; path='/'; trust=$false; disable=$true; expected=1; curlError=60},
  @{name='callback-cannot-disable-host-check'; host='127.0.0.1'; path='/'; trust=$true; disable=$true; expected=1; curlError=51},
  @{name='redirect-to-wrong-host'; host='localhost'; path='/redirect'; trust=$true; disable=$false; expected=1; curlError=51}
)
$runs=@()
foreach($arch in @('x64','x86')){
  $kernel=(Resolve-Path -LiteralPath $(if($arch -eq 'x64'){$KernelX64}else{$KernelX86})).Path
  $probe=(Resolve-Path -LiteralPath $(if($arch -eq 'x64'){$ProbeX64}else{$ProbeX86})).Path
  foreach($case in $cases){
    $arguments=@($kernel,"https://$($case.host):$Port$($case.path)")
    if($case.trust){$arguments+=$cert}
    if($case.disable){if(-not $case.trust){$arguments+='-'};$arguments+='try-disable'}
    $ErrorActionPreference='Continue'
    $output=(& $probe @arguments 2>&1 | Out-String).Trim()
    $code=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    $match=[regex]::Match($output,'done=1 result=(\d+) reason=([^\r\n]*)')
    $actual=if($match.Success){[int]$match.Groups[1].Value}else{$null}
    $curlError=if($case.curlError){$output -match "error reason: $($case.curlError),"}else{$true}
    $runs+=[ordered]@{architecture=$arch;scenario=$case.name;kernelSha256=(Get-FileHash -LiteralPath $kernel -Algorithm SHA256).Hash.ToLowerInvariant();probeSha256=(Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash.ToLowerInvariant();exitCode=$code;expectedResult=$case.expected;actualResult=$actual;expectedCurlError=$case.curlError;passed=($code -eq 0 -and $actual -eq $case.expected -and $curlError);output=$output}
  }
}
$report=[ordered]@{schema='soui.tls-kernel-validation/v1';observedAt=[DateTime]::UtcNow.ToString('o');certificateSha256=(Get-FileHash -LiteralPath $cert -Algorithm SHA256).Hash.ToLowerInvariant();port=$Port;releaseReady=$false;passed=(@($runs|Where-Object {-not $_.passed}).Count -eq 0);runs=$runs}
$path=[IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
[IO.File]::WriteAllText($path,($report|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output "TLS kernel validation: $($report.passed) / $path"
if(-not $report.passed){exit 1}
