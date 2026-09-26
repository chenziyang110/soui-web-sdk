'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=__dirname,arch=process.argv[2],phase=process.argv[3];assert(['x86','x64'].includes(arch));assert(['red','green'].includes(phase));
const build=path.join(root,'builds',arch+'-'+phase),env={...process.env,VcpkgEnabled:'false',VcpkgAutoLink:'false',VcpkgEnableManifest:'false',PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
const source=path.join(root,'source-snapshots',arch+'-'+phase);assert(!fs.existsSync(source));fs.cpSync(path.join(root,'consumer'),source,{recursive:true,errorOnExist:true,force:false});
function run(name,program,args,expected=0){const r=cp.spawnSync(process.execPath,[path.join(root,'Capture-Command.cjs'),arch+'-'+phase+'-'+name,'180000',program,...args],{env,windowsHide:true,stdio:'inherit'});assert.equal(r.status,expected,name);const raw=JSON.parse(fs.readFileSync(path.join(root,'command-logs',arch+'-'+phase+'-'+name,'result.json')));assert.equal(raw.exitCode,expected);assert.equal(raw.error,null);assert.equal(raw.timedOut,false);}
run('configure','cmake.exe',['-S',source,'-B',build,'-G','Visual Studio 17 2022','-A',arch==='x86'?'Win32':'x64','-T','v143','-DCMAKE_SYSTEM_VERSION=10.0.19041.0','-DSouiSdk_DIR='+path.join(root,'inputs/install',arch,'lib/cmake/SouiSdk')]);
run('compile','cmake.exe',['--build',build,'--config','Release','--target',...(phase==='red'?['SupervisorClockTests']:['SupervisorClockTests','InstalledRenewalProbe']),'--parallel','2']);
run('clock-check',path.join(build,'Release/SupervisorClockTests.exe'),[],phase==='red'?1:0);
if(phase==='green')run('native-self-check',path.join(build,'bin/Release/InstalledRenewalProbe.exe'),['--self-check']);
console.log(JSON.stringify({arch,phase,expectedRedFailure:phase==='red',guiExecuted:false}));
