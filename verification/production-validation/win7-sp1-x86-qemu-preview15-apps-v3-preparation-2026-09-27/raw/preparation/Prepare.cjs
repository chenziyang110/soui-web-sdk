'use strict';
// Prepare fresh preview.15 input. Historical guest inputs remain byte-for-byte unchanged.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,parent='C:/SOUI-SDK-production-validation/preview11-win7-preparation-v3-20260926';
const oldSource='a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550',source='b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07';
const oldBuild='7e000158-98f0-4349-854e-15c581663737',build='b8c26d8d-7144-4eb0-b889-0034ec8029fb';
const id=require('C:/SOUI-SDK-development/preview14-scheduler/tools/internal/delivery-identity.cjs');
const parentFreeze='10c9a4e90cf5e25a2c98d0f6107eb7881a5ab39caf768973d761a1045566826d';
assert.equal(id.hash(fs.readFileSync(path.join(parent,'freeze.json'))),parentFreeze);
const adapt=s=>s.replaceAll(oldSource,source).replaceAll(oldBuild,build).replaceAll('preview11','preview15').replaceAll('preview.11','preview.15').replaceAll('Preview.11','Preview.15');
const write=(name,text)=>{const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,text,{flag:'wx'});};
for(const name of ['run-preview11.cmd','run-apps.ps1','verify-apps.ps1','export-serial.ps1']) {
 let script=adapt(fs.readFileSync(path.join(parent,'volume/soui-input',name),'utf8'));
 if(name==='run-apps.ps1') {
  const diagnostic=fs.readFileSync(path.join(root,'Process-Diagnostic.ps1'),'utf8');
  script=script.replace('function Run-App(',diagnostic+'\nfunction Run-App(');
  script=script.replace('$timedOut=$false;$stillRunning=$false;','$diagnosticTaken=$false;$timedOut=$false;$stillRunning=$false;');
  const guard='if($watch.ElapsedMilliseconds -ge 60000){$timedOut=$true;break}';assert(script.includes(guard));
  script=script.replace(guard,guard+`\n      if(-not $diagnosticTaken -and $watch.ElapsedMilliseconds -ge 55000){$diagnosticTaken=$true;Write-ProcessDiagnostic $Name $process $watch $log}`);
 }
 write('harness/'+adapt(name),script);
}
let prep=adapt(fs.readFileSync('C:/SOUI-SDK-production-validation/preview11-win7-preparation-20260926/Prepare-Payload.cjs','utf8'));
prep=prep.replace('C:/SOUI-SDK-development/preview9-tray','C:/SOUI-SDK-development/preview14-scheduler');
prep=prep.replace("path.join(root, 'transfer')","path.join(root, 'volume/soui-input')");
prep=prep.replace('fs.mkdirSync(transfer);','fs.mkdirSync(transfer,{recursive:true});');
prep=prep.replace("appTimeoutMs: 60000, packages, transferFiles,",`appTimeoutMs: 60000, diagnosticsVersion:2, layoutVersion:3, inputSubdir:'soui-input', mountRoot:path.join(root,'volume'),
  harnessParentFreezeSha256:'${parentFreeze}', closeDiagnostics:{atMs:55000,deadlineMs:60000,threadLimit:128,kind:'read-only-process-thread-snapshot'}, packages, transferFiles,`);
write('Prepare-Payload.cjs',prep);
let verifier=adapt(fs.readFileSync(path.join(parent,'Verify-AppSerial.cjs'),'utf8'));
verifier=verifier.replace("input.parentFreezeSha256 !== '20f5b68956e9483dfeff26349bd81d49bcd81e4f6aeb8bf0ee7d462588dc9d13'",`input.harnessParentFreezeSha256 !== '${parentFreeze}' || input.closeDiagnostics?.atMs !== 55000 || input.closeDiagnostics?.deadlineMs !== 60000 || input.closeDiagnostics?.threadLimit !== 128`);
verifier=verifier.replace("const optional = ['starter-navigation.log', 'workbench-default-navigation.log', 'workbench-react-navigation.log'];","const optional = ['starter-navigation.log', 'workbench-default-navigation.log', 'workbench-react-navigation.log', ...['starter','workbench-default','workbench-react'].map(n=>n+'-process-diagnostic.txt')];");
write('Verify-AppSerial.cjs',verifier);
write('SelfTest-Serial.cjs',adapt(fs.readFileSync(path.join(parent,'SelfTest-Serial.cjs'),'utf8')));
write('SelfTest-Diagnostics.ps1',fs.readFileSync(path.join(parent,'SelfTest-Diagnostics.ps1')));
write('harness-parent-freeze.json',fs.readFileSync(path.join(parent,'freeze.json')));
write('preparation-origin.json',id.encoded({candidateCommit:'6702e94397f1339c466d86a6f7affdf1f3947703',version:'0.3.0-preview.15',sourceSha256:source,buildId:build,harnessParentFreezeSha256:parentFreeze,previousGuestApplicationPassInherited:false,guestExecuted:false,releaseReady:false}));
console.log(JSON.stringify({status:'current-guest-scripts-prepared',version:'0.3.0-preview.15',guestExecuted:false}));
