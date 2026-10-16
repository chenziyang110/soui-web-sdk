# A build-owned immutable capture of the appendable, configuration-shared log.
# Source/graph/compiler verification remains with Observe-LlvmBuild.ps1.
function Copy-SouiCMakeConfigureLog {
  param([Parameter(Mandatory)][string]$BuildDirectory,[Parameter(Mandatory)][string]$EvidenceDirectory)
  $live=Join-Path $BuildDirectory 'CMakeFiles/CMakeConfigureLog.yaml'
  $target=Join-Path $EvidenceDirectory 'cmake-configure-log.input.yaml'
  $before=ObsPin $live
  Need ($before.bytes-gt0-and$before.bytes-le67108864) 'Bounded nonempty configure log required'
  [void](Plain $EvidenceDirectory)
  Need (-not(Test-Path -LiteralPath $target)) 'Fresh configure log snapshot required'
  [IO.File]::Copy($live,$target,$false)
  $snapshot=ObsPin $target
  Need ($snapshot.bytes-eq$before.bytes-and$snapshot.sha256-ceq$before.sha256) 'Configure log snapshot differs'
  [void](MatchPin $before)
  return [ordered]@{schema='soui.cmake-configure-log-capture/v1';live=$before;snapshot=$snapshot}
}
