param([Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$sourceRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Test-Path -LiteralPath $OutputPath){throw 'Fresh pure Source result required'}
function Parse([string]$path){$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors);if($errors.Count){throw ('Source AST errors: '+$path)};return $ast}
function FunctionText($ast,[string]$name){$found=@($ast.FindAll({param($n)$n-is[Management.Automation.Language.FunctionDefinitionAst]-and$n.Name-ceq$name},$true));if($found.Count-ne1){throw ('Unique actual function required: '+$name)};return $found[0].Extent.Text}
$candidatePath=Join-Path $sourceRoot 'tools/internal/Observe-LlvmBuild.ps1';$oldPath=Join-Path $sourceRoot 'baseline/adopted/tools/internal/Observe-LlvmBuild.ps1';$helperPath=Join-Path $sourceRoot 'tools/internal/Tool-FileIdentity.ps1'
$candidate=Parse $candidatePath;$old=Parse $oldPath;$helper=Parse $helperPath
foreach($name in @('Need','Norm','LocalToolSpelling','ToolNorm')){. ([ScriptBlock]::Create((FunctionText $candidate $name)))}
$newTool=FunctionText $candidate 'ToolIdentity';$oldTool=FunctionText $old 'ToolIdentity';$newWrapper=FunctionText $candidate 'ResourceWrapperIdentity';$oldWrapper=FunctionText $old 'ResourceWrapperIdentity'
$guards=@($helper.FindAll({param($n)$n-is[Management.Automation.Language.IfStatementAst]-and$n.Extent.Text.Contains("throw 'Absolute local tool path required'")},$true))
if($guards.Count-ne1){throw 'Exact original local helper guard required'}
$script:originalGuard=[ScriptBlock]::Create($guards[0].Extent.Text)
$first=[IO.File]::ReadAllText((Join-Path $sourceRoot 'fixtures/first-create-paused.ndjson'))|ConvertFrom-Json
$frozenPin=[IO.File]::ReadAllText((Join-Path $sourceRoot 'fixtures/powershell-profile-pin.json'))|ConvertFrom-Json
$script:modelCalls=0;$script:modelFault='';$script:observedArguments=[Collections.Generic.List[string]]::new()
# This inert replacement calls the exact old helper spelling guard, then models
# a successful dual-held identity receipt. It never dot-sources/executes the
# helper, performs native FileID work, invokes Add-Type or opens a tool file.
function Assert-ToolFileIdentity {
 param([string]$ObservedPath,$ExpectedPin)
 $script:modelCalls++;$script:observedArguments.Add($ObservedPath)
 foreach($path in @($ObservedPath,[string]$ExpectedPin.path)){. $script:originalGuard}
 Need ((Norm $ObservedPath)-ceq(Norm $ExpectedPin.path)) 'Modeled different physical tool file'
 if($script:modelFault){throw ('Modeled held identity failure: '+$script:modelFault)}
 return [pscustomobject]@{ObservedPath=$ObservedPath;ExpectedPath=$ExpectedPin.path;FinalPath=('\\?\'+$ObservedPath);VolumeSerial='1e239361';FileIndex='00070000000011c9';Bytes=$ExpectedPin.bytes;Sha256=$ExpectedPin.sha256;modelOnly=$true}
}
function ResetModel {
 $script:modelCalls=0;$script:modelFault='';$script:observedArguments.Clear();$script:toolIdentities=@{};$script:aliases=@{}
 $script:roleNames=@('CLANG','LINKER','ARCHIVER','RC','MT','NINJA','CMD','POWERSHELL')
 $script:profile=[pscustomobject]@{ToolPins=[pscustomobject]@{POWERSHELL=$frozenPin}}
}
$cases=[Collections.Generic.List[object]]::new()
function Case([string]$name,[scriptblock]$work,[bool]$reject=$false,[string]$reason=''){
 $caught=$false;$diagnostic=$null
 try{&$work|Out-Null}catch{$caught=$true;$diagnostic=$_.Exception.Message}
 if($caught-ne$reject){throw ('Unexpected pure outcome: '+$name+' '+$diagnostic)}
 if($reject-and$reason-and-not$diagnostic.Contains($reason)){throw ('Unexpected pure rejection: '+$name+' '+$diagnostic)}
 $cases.Add([ordered]@{name=$name;passed=$true;rejected=$caught;diagnostic=$diagnostic;modelOnly=$true})
}
ResetModel;. ([ScriptBlock]::Create($oldTool))
Case 'exact-current-first-CREATE-old-RED' {ToolIdentity $first.imageFile.finalPath $first.role} $true 'Absolute local tool path required'
Need ($script:modelCalls-eq1-and$script:toolIdentities.Count-eq0-and$script:aliases.Count-eq0) 'Old rejection cannot produce an identity alias'
ResetModel;. ([ScriptBlock]::Create($newTool))
Case 'exact-current-first-CREATE-new-GREEN-model' {
 $raw=$first.imageFile.finalPath;$receipt=ToolIdentity $raw $first.role
 Need ($receipt.modelOnly-eq$true-and$receipt.ObservedPath-ceq$raw.Substring(4)-and$script:modelCalls-eq1) 'New spelling must still call modeled unchanged identity helper'
 Need ($script:aliases.ContainsKey((Norm $raw))-and$script:aliases.ContainsKey((Norm $raw.Substring(4)))) 'Raw and checked spellings must both be proven aliases'
 Need ($script:aliases[(Norm $raw)]-ceq(Norm $frozenPin.path)-and$script:aliases[(Norm $raw.Substring(4))]-ceq(Norm $frozenPin.path)) 'Aliases must bind full frozen profile pathname'
 Need ($first.pid-eq100724-and$first.creationFileTime-ceq'134364790062696673'-and$first.role-ceq'POWERSHELL'-and$first.imageFile.finalPath.StartsWith('\\?\',[StringComparison]::Ordinal)) 'Original first CREATE remains unchanged'
}
Case 'raw-observation-and-checked-alias-command-model' {
 $expected=(Norm $frozenPin.path)+' /DKEEP_CASE=VALUE'
 Need ((ToolNorm ($first.imageFile.finalPath+' /DKEEP_CASE=VALUE'))-ceq$expected) 'Raw alias changes only physically proven tool spelling'
 Need ((ToolNorm ($first.imageFile.finalPath.Substring(4)+' /DKEEP_CASE=VALUE'))-ceq$expected) 'Checked alias changes only physically proven tool spelling'
}
Case 'raw-spelling-cache-key-retained' {$before=$script:modelCalls;[void](ToolIdentity $first.imageFile.finalPath $first.role);Need ($script:modelCalls-eq$before) 'Raw key cache preserved';Need ($script:toolIdentities.ContainsKey('POWERSHELL|'+(Norm $first.imageFile.finalPath))) 'Raw key spelling preserved'}
foreach($value in @('C:\tools\tool.exe','c:/tools/tool.exe','D:\space folder\tool.exe','E:/混合😀/tool.exe')){
 $valueCopy=$value;Case ('ordinary-spelling-byteexact-'+$value) {Need ((LocalToolSpelling $valueCopy)-ceq$valueCopy) 'Ordinary spelling must remain exact'}
}
foreach($value in @('\\?\C:\tools\tool.exe','\\?\d:\space folder\tool.exe','\\?\E:\混合😀\tool.exe')){
 $valueCopy=$value;Case ('extended-fixed-four-prefix-'+$value) {Need ((LocalToolSpelling $valueCopy)-ceq$valueCopy.Substring(4)) 'Only fixed four prefix removal'}
}
$negative=@(
 '', 'C:relative.exe','relative.exe','\rooted.exe','\\server\share\tool.exe',
 '\\?\UNC\server\share\tool.exe','\\.\C:\tools\tool.exe','\??\C:\tools\tool.exe',
 '\\?\GLOBALROOT\Device\HarddiskVolume1\tool.exe','\\?\Volume{123}\tool.exe',
 '\\?\C:/tools/tool.exe','//?/C:/tools/tool.exe','\\?\C:\tools/tool.exe',
 '\\?\C:relative.exe','\\?\C:\','\\?\D:\tools\tool.exe:stream',
 'C:\tools\tool.exe:stream','C:\tools\tool.exe::$DATA','C:\tools\other:C:\tool.exe',
 'C:\tools\..\tool.exe','C:\tools\.\tool.exe','C:\tools\\tool.exe','C:/tools//tool.exe',
 'C:\tools\tool.exe.','C:\tools\tool.exe ','C:\tools.\tool.exe','C:\tools \tool.exe',
 'C:\tools\*.exe','C:\tools\?.exe','C:\tools\tool".exe','C:\tools\tool|.exe',
 'C:\tools\NUL.exe','C:\CON\tool.exe','C:\tools\COM1.exe','C:\tools\LPT9',
 ('C:\tools\a'+[char]0+'b.exe'),('C:\tools\a'+[char]9+'b.exe'),('C:\tools\a'+[char]10+'b.exe'),('C:\tools\a'+[char]13+'b.exe'),('C:\tools\a'+[char]127+'b.exe'),('C:\tools\a'+[char]133+'b.exe')
)
for($i=0;$i-lt$negative.Count;$i++){$valueCopy=$negative[$i];Case ('invalid-local-spelling-'+$i) {LocalToolSpelling $valueCopy} $true}
Case 'null-spelling-rejected' {LocalToolSpelling $null} $true
ResetModel;. ([ScriptBlock]::Create($newTool))
Case 'same-basename-unpinned-location-rejected-model' {ToolIdentity 'C:\untrusted\pwsh.exe' 'POWERSHELL'} $true 'Modeled different physical tool file'
Need ($script:aliases.Count-eq0-and$script:toolIdentities.Count-eq0) 'Identity rejection cannot publish aliases/cache'
Case 'bad-role-before-identity-model' {ToolIdentity $first.imageFile.finalPath 'FAKE'} $true 'Fixed role only'
foreach($fault in @('different-FileID','wrong-hash','wrong-bytes','held-before-after-changed')){
 ResetModel;$script:modelFault=$fault;$faultCopy=$fault
 Case ('held-identity-fault-rejected-model-'+$fault) {ToolIdentity $first.imageFile.finalPath 'POWERSHELL'} $true 'Modeled held identity failure'
 Need ($script:modelCalls-eq1-and$script:aliases.Count-eq0-and$script:toolIdentities.Count-eq0) 'No alias when original identity helper rejects'
}
$physicalGuards=@($old.FindAll({param($n)$n-is[Management.Automation.Language.CommandAst]-and$n.Extent.Text.Contains("'Independent current fixed-role physical image'")},$true))
if($physicalGuards.Count-ne1){throw 'Original CREATE sha/bytes/machine guard required'}
$physicalGuard=[ScriptBlock]::Create($physicalGuards[0].Extent.Text)
ResetModel;. ([ScriptBlock]::Create($newTool))
foreach($field in @('sha256','bytes','machine')){
 $r=($first|ConvertTo-Json -Depth 20|ConvertFrom-Json)
 if($field-ceq'sha256'){$r.imageFile.sha256='a'*64}elseif($field-ceq'bytes'){$r.imageFile.bytes='1'}else{$r.imageFile.machine=332}
 Case ('unchanged-current-CREATE-physical-gate-'+$field) {[void](ToolIdentity $r.imageFile.finalPath $r.role);. $physicalGuard} $true 'Independent current fixed-role physical image'
}
function MatchPin($pin){return $pin} # Inert Source-only mock; no file check actor.
$resourcePin=[pscustomobject]@{path='C:\cmake\cmcldeps.exe';bytes=123;sha256=('a'*64)}
$cfg=[pscustomobject]@{resourceWrapperPin=$resourcePin;dependencyPins=@($resourcePin)}
ResetModel;. ([ScriptBlock]::Create($oldWrapper))
Case 'resource-wrapper-same-seam-old-RED' {ResourceWrapperIdentity '\\?\C:\cmake\cmcldeps.exe'} $true 'Absolute local tool path required'
ResetModel;. ([ScriptBlock]::Create($newWrapper))
Case 'resource-wrapper-checked-spelling-new-GREEN-model' {$r=ResourceWrapperIdentity '\\?\C:\cmake\cmcldeps.exe';Need ($r.ObservedPath-ceq'C:\cmake\cmcldeps.exe'-and$script:modelCalls-eq1-and$script:aliases.Count-eq2) 'Wrapper uses same strict spelling seam'}
ResetModel;$cfg=[pscustomobject]@{resourceWrapperPin=$resourcePin;dependencyPins=@()}
Case 'resource-wrapper-unchanged-dependency-gate-model' {ResourceWrapperIdentity '\\?\C:\cmake\cmcldeps.exe'} $true 'whole frozen dependency envelope'
$oldText=[IO.File]::ReadAllText($oldPath);$newText=[IO.File]::ReadAllText($candidatePath);$localText=FunctionText $candidate 'LocalToolSpelling'
Case 'all-outside-three-function-delta-byteexact' {Need ($newText.Replace($localText+"`r`n",'').Replace($newTool,$oldTool).Replace($newWrapper,$oldWrapper)-ceq$oldText) 'Only literal spelling boundary functions may change'}
Case 'original-helper-body-byteexact' {Need ([IO.File]::ReadAllText($helperPath)-ceq[IO.File]::ReadAllText((Join-Path $sourceRoot 'baseline/adopted/tools/internal/Tool-FileIdentity.ps1'))) 'Original ordinary helper contract must not change'}
Case 'original-debug-CREATE-call-and-raw-image-retained' {Need ($newText.Contains('$receipt=ToolIdentity $r.imageFile.finalPath $r.role')-and$newText.Contains('image=$r.imageFile.finalPath;identity=$receipt')) 'No raw CREATE rewrite or basename role approval'}
Case 'original-helper-import-binding-byteexact' {Need ($newText.Contains("`$helper=Join-Path `$PSScriptRoot 'Tool-FileIdentity.ps1';Need ((Norm `$profile.CMakeDefinitions.SOUI_TOOL_IDENTITY_HELPER)-ceq(Norm `$helper)) 'Fixed product helper only'")) 'Helper import seam stays source/profile bound'}
$result=[ordered]@{schema='soui.portable-sdk-semantic-reader-pure/1';sourceOnly=$true;sourceAuthor='/root/business_v2_review';source=(Get-FileHash -LiteralPath $candidatePath).Hash.ToLowerInvariant();firstCreateSequence=$first.sequence;firstCreatePid=$first.pid;firstCreateFileTime=$first.creationFileTime;oldExactOriginalGuardRed=$true;newCompleteActualFunctionsModelGreen=$true;helperOriginalGuardOnlyInvoked=$true;helperBodyOrAddTypeExecuted=$false;nativeFileIdentityActuallyProved=$false;allOutsideDeltaByteExact=$true;cases=$cases.ToArray();passed=$cases.Count;failed=0;actualReaderExecuted=$false;actorCalls=0;actualNativeBuild=$null;oldSdkBuildAccepted=$false;qualificationPassed=$false;releaseReady=$false}
$outJson=$result|ConvertTo-Json -Depth 30
[IO.File]::WriteAllText($OutputPath,$outJson+"`n",[Text.UTF8Encoding]::new($false))
[Console]::Out.WriteLine((ConvertTo-Json -Compress -InputObject ([ordered]@{passed=$cases.Count;failed=0;sourceOnly=$true;actualReaderExecuted=$false;oldSdkBuildAccepted=$false;releaseReady=$false})))
