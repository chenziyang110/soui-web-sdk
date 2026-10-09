[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$taskPacketRoot='F:/work/sm05-broker/out/reference-frontend-v3-root-39eb38fb-a368-4869-a5fa-77b8a817e11b'
$taskLease=[Threading.Mutex]::new($false,'Local\SouiDevKit.DesktopValidation')
$taskAcquired=$false
$taskEnvNames=@('NODE_OPTIONS','NODE_PATH','NPM_CONFIG_USERCONFIG','NPM_CONFIG_GLOBALCONFIG','NPM_CONFIG_CACHE','NODE_ENV')
$taskPriorEnv=@{}
foreach($taskName in $taskEnvNames){$taskPriorEnv[$taskName]=[Environment]::GetEnvironmentVariable($taskName,'Process')}
try {
 try {$taskAcquired=$taskLease.WaitOne(0)} catch [Threading.AbandonedMutexException] {$taskAcquired=$true}
 if(-not $taskAcquired){throw 'Desktop validation lease is busy; no execution.'}
 $taskPins=@{'package.json'='ce6286387be9664f90040ad80e36f37fc942422db06b64356267d3bc5bd9cee4';'run-root.cjs'='e6b93b108223475a639b541bdc57614a53c9fea3cd1d232b38b5bc6fd59efb45';'tools.json'='c073ad93d7782a7b8b7d56286565bb54c4223374dc98ff76b21fa433285ff1d6'}
 foreach($taskName in $taskPins.Keys){if((Get-FileHash -LiteralPath (Join-Path $taskPacketRoot $taskName) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskPins[$taskName]){throw 'Root package changed'}}
 $taskTools=Get-Content -LiteralPath (Join-Path $taskPacketRoot 'tools.json') -Raw|ConvertFrom-Json
 if((Get-FileHash -LiteralPath $taskTools.node.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskTools.node.sha256){throw 'Node host changed'}
 foreach($taskName in $taskEnvNames){[Environment]::SetEnvironmentVariable($taskName,$null,'Process')}
 & $taskTools.node.path (Join-Path $taskPacketRoot 'run-root.cjs') --root-gate ROOT_W6_REFERENCE_V5_FRONTEND_V3 --package-sha 'ce6286387be9664f90040ad80e36f37fc942422db06b64356267d3bc5bd9cee4'
 $taskExit=$LASTEXITCODE
 if($taskExit -ne 0){exit $taskExit}
} finally {
 try {foreach($taskName in $taskEnvNames){[Environment]::SetEnvironmentVariable($taskName,$taskPriorEnv[$taskName],'Process')}} finally {if($taskAcquired){$taskLease.ReleaseMutex()};$taskLease.Dispose()}
}
