'use strict';
// Read and copy evidence only. Never launches applications, build tools, or VMs.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const base='C:/SOUI-SDK-production-validation';
const prep=path.join(base,'preview11-win7-preparation-v3-20260926');
const destination='C:/SOUI-SDK-development/preview9-tray/verification/production-validation/win7-sp1-x86-qemu-preview11-apps-v3-preparation-2026-09-26';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const json=v=>JSON.stringify(v,null,2)+'\n';
const expectedFreeze='10c9a4e90cf5e25a2c98d0f6107eb7881a5ab39caf768973d761a1045566826d';
const frozenBytes=fs.readFileSync(path.join(prep,'freeze.json'));
if(sha(frozenBytes)!==expectedFreeze)throw new Error('v3 freeze changed');
const frozen=JSON.parse(frozenBytes),expectedFiles=new Map(frozen.files.map(f=>[f.path,f]));
const input=JSON.parse(fs.readFileSync(path.join(prep,'input.json')));
const gateSource=path.join(base,'preview11-win7-v3-launch-gate-20260926.json');
const gateBytes=fs.readFileSync(gateSource),gate=JSON.parse(gateBytes.toString('utf8').replace(/^\uFEFF/,''));
if(gate.attemptExists!==false || !gate.processes.some(p=>p.ProcessId===72440 && p.Name==='qemu-system-x86_64.exe' && p.CommandLine.includes('vds-format-v1-20260926')))throw new Error('Launch gate identity differs');
if(fs.existsSync(destination))throw new Error('Refuse to overwrite evidence destination');
const sources=[];
for(const name of ['README.md','input.json','freeze.json','parent-freeze.json','input.parent.json','build-Release.json',
  'layout-preparation.json','Prepare-Layout.cjs','Freeze-Preparation.cjs','Verify-Frozen.cjs','Verify-AppSerial.cjs',
  'SelfTest-Serial.cjs','SelfTest-Diagnostics.ps1','self-test-only-v4-results.json','syntax-check.json','host-diagnostic-self-test.log',
  ...['run-preview11.cmd','run-apps.ps1','verify-apps.ps1','export-serial.ps1'].map(n=>'harness/'+n),
  ...['SouiStarter-payload.tsv','SouiWorkbench-payload.tsv','transfer-files.tsv'].map(n=>'volume/soui-input/'+n),
  ...['transfer-diagnostics.txt','transfer-entries.tsv','transfer-lines.tsv','transfer-manifest-raw.tsv'].map(n=>'host-transfer-diagnostics/'+n),
  ...['serial-export.txt','guest-identity.txt','harness.log','harness-exception.txt','run-exitcode.txt','transfer-diagnostics.txt',
    'transfer-entries.tsv','transfer-lines.tsv','transfer-manifest-raw.tsv'].map(n=>'parent-attempt2/'+n)]){
  sources.push({source:path.join(prep,name),target:'raw/preparation/'+name,frozenPath:name});
}
for(const name of ['preview11-qemu-launch-v3.ps1','preview11-qemu-monitor-v3.cjs','preview11-win7-v3-launch-gate-20260926.json']){
  sources.push({source:path.join(base,name),target:'raw/launch/'+name});
}
sources.push({source:__filename,target:'raw/archive/'+path.basename(__filename)});
fs.mkdirSync(destination);
const manifest=[];
for(const entry of sources){
  const stat=fs.lstatSync(entry.source);
  if(!stat.isFile()||stat.isSymbolicLink())throw new Error('Evidence source is not a regular file: '+entry.source);
  const bytes=fs.readFileSync(entry.source),hash=sha(bytes);
  if(entry.frozenPath && entry.frozenPath!=='freeze.json'){
    const record=expectedFiles.get(entry.frozenPath);
    if(!record||record.bytes!==bytes.length||record.sha256!==hash)throw new Error('Frozen preparation source changed: '+entry.frozenPath);
  }
  const target=path.join(destination,entry.target);
  fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});
  if(!fs.readFileSync(target).equals(bytes)||!fs.readFileSync(entry.source).equals(bytes))throw new Error('Evidence changed during copy: '+entry.source);
  manifest.push({path:entry.target,sourcePath:path.resolve(entry.source),bytes:bytes.length,sha256:hash});
}
manifest.sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);
const raw={schema:'soui.win7-preview11-v3-preparation-archive/v1',archivedAt:new Date().toISOString(),
  status:'prepared-launch-gate-blocked-before-attempt',sourceRoot:prep,freezeSha256:expectedFreeze,
  inputSha256:sha(fs.readFileSync(path.join(prep,'input.json'))),files:manifest,
  rawFileCount:manifest.length,rawBytes:manifest.reduce((sum,f)=>sum+f.bytes,0),
  guestExecuted:false,attemptCreated:false,functionalSmokePassed:false,realRenewalVerified:false,sourceDeliveryVerified:false,
  benchmarkValid:false,releaseReady:false,productionReady:false};
fs.writeFileSync(path.join(destination,'raw-manifest.json'),json(raw),{flag:'wx'});
const fields={
  'input.version':input.version,'input.buildId':input.buildId,'input.sourceSha256':input.sourceSha256,
  expectedFreeze,'raw.inputSha256':raw.inputSha256,'gate.checkedAt':gate.checkedAt,
  'raw.rawFileCount':raw.rawFileCount,"raw.rawBytes.toLocaleString('en-US')":raw.rawBytes.toLocaleString('en-US')
};
const readme="# preview.11 Win7 x86：v3 子目录输入已冻结，第三轮未启动\n\n状态为 **prepared-launch-gate-blocked-before-attempt**。本目录归档 v3 的文件准备及门禁证据；第三轮客体、三个应用均未启动，没有验证布局修正后的实际结果，也没有新增 Win7 应用 smoke 通过。\n\n## 原因与最小布局变更\n\n[第二轮原始串口](raw/preparation/parent-attempt2/serial-export.txt)及其八份原文件来自真实 v2 失败：清单 809 字节、9 行记录匹配，实际根目录枚举 11 项，额外 `D:\\$RECYCLE.BIN` 为隐藏系统目录。原“无子目录、恰好 10 文件”的检查正确拒绝，退出 1，尚未进入 ZIP、普通文件客体散列或应用执行。完整第二轮记录见[原失败归档](../win7-sp1-x86-qemu-preview11-apps-attempt2-2026-09-26/README.md)。证据没有说明是谁创建该目录，也不倒填首次失败的未知枚举。\n\nv3 把十个冻结文件放到卷内 `soui-input` 子目录；卷根系统元数据在输入目录之外。所有客体脚本、TSV、两个 ZIP 与 v2 原字节一致；546 个完整应用文件（含各 5 个 legal）也未修改。没有忽略 `$RECYCLE.BIN` 或放宽集合/散列规则，任何额外项若出现在输入目录内仍拒绝。\n\n[input.json](raw/preparation/input.json)明确 `layoutVersion=3`、`diagnosticsVersion=2`、宿主 `mountRoot` 和 `inputSubdir=soui-input`；[校验器](raw/preparation/Verify-AppSerial.cjs)要求实际客体 Transfer 为对应子目录，保留全部退出码、载荷身份、ready 与关闭顺序门槛。[父 freeze](raw/preparation/parent-freeze.json)及[父输入](raw/preparation/input.parent.json)绑定 v2 来源。\n\n## 身份与已完成检查\n\n- 候选：`${input.version}`、x86 Release；build ID `${input.buildId}`。\n- compiled source SHA-256：`${input.sourceSha256}`；不声称本轮重新编译。\n- v3 freeze SHA-256：`${expectedFreeze}`。\n- v3 input SHA-256：`${raw.inputSha256}`。\n- [synthetic 自检](raw/preparation/self-test-only-v4-results.json)：1 个完整样本接受、27 个负例拒绝，包含输入目录内额外普通文件、额外回收站目录、错误卷根/子目录，以及 hash 正确但非零退出或缺失/乱序关闭；它不是客体执行证据。\n- [宿主诊断自检](raw/preparation/host-diagnostic-self-test.log)：仅调用诊断函数，并以原 guard 验证多一个 FileInfo/DirectoryInfo 均拒绝；未执行客体脚本主体、ZIP 工具或应用。[语法记录](raw/preparation/syntax-check.json)也不代表 Win7 运行。\n\n## 第三轮在创建 attempt 前被门禁拒绝\n\n[门禁原始 JSON](raw/launch/preview11-win7-v3-launch-gate-20260926.json)记录检查时间 `${gate.checkedAt}`、`attemptExists=false`，当时存在其他任务 QEMU PID 72440（`vds-format-v1-20260926`）。[启动脚本](raw/launch/preview11-qemu-launch-v3.ps1)在创建 attempt 和启动 QEMU 前检查这些负载，故本次没有第三轮 QEMU PID、桌面截图、客体命令或串口结果；未停止其他任务。\n\n冻结输入原目录仍为 `C:\\SOUI-SDK-production-validation\\preview11-win7-preparation-v3-20260926`。待宿主全部桌面/QEMU 负载空闲，重新做门禁和冻结复核后，新 attempt 应使用下面精确挂载路径；不能误挂其内的 `soui-input`：\n\n```text\n-drive file=fat:ro:C:\\SOUI-SDK-production-validation\\preview11-win7-preparation-v3-20260926\\volume,format=raw,if=ide,index=1\n```\n\n确认 fresh snapshot 的 Win7 桌面和盘符后，仅发送一次（D: 为实际传输盘时）：\n\n```cmd\ncmd.exe /c D:\\soui-input\\run-preview11.cmd\n```\n\n[monitor 脚本](raw/launch/preview11-qemu-monitor-v3.cjs)只允许该冻结命令。端口、PID 和串口路径必须在实际启动时重新确定；这里保存的脚本是尚未越过门禁的准备，不能当作启动成功。完整准备说明见[原字节 README](raw/preparation/README.md)。\n\n## 归档范围\n\n[raw-manifest.json](raw-manifest.json)列出 ${raw.rawFileCount} 份原字节文件，共 ${raw.rawBytes.toLocaleString('en-US')} 字节，记录各来源、长度与 SHA-256；复制后复核源文件和副本一致。保存只读验证脚本、输入/freeze、构建记录、必要 v2 来源、合成结果摘要及 launcher/monitor/门禁；不保存应用 ZIP/二进制、546 个 payload 文件、完整 synthetic 串口大样本或系统磁盘，因此此小归档不是可独立运行的传输卷。\n\n`guestExecuted=false`、`attemptCreated=false`、`functionalSmokePassed=false`、`realRenewalVerified=false`、`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`、`productionReady=false`。后续第三轮必须独立记录，不改写前两次失败，也不把本次准备当作修复通过。\n".replace(/\$\{([^}]+)\}/g,(_,key)=>{
  if(!Object.hasOwn(fields,key))throw new Error('Unknown README field: '+key);
  return String(fields[key]);
});
fs.writeFileSync(path.join(destination,'README.md'),readme,{flag:'wx'});
process.stdout.write(json({status:'archived-preparation-only',destination,rawFileCount:raw.rawFileCount,rawBytes:raw.rawBytes,
  totalFiles:raw.rawFileCount+2,rawManifestSha256:sha(fs.readFileSync(path.join(destination,'raw-manifest.json'))),
  readmeSha256:sha(fs.readFileSync(path.join(destination,'README.md'))),freezeSha256:expectedFreeze,guestExecuted:false,releaseReady:false}));
