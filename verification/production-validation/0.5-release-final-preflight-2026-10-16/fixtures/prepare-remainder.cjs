'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
let value=fs.readFileSync(__dirname+'/remaining-tests.cjs','utf8');
const anchor="const folder=__dirname+'/remaining-tests';fs.mkdirSync(folder);";
assert.equal(value.split(anchor).length,2);
value=value.replace(anchor,"selected.splice(0,5);\nconst folder=__dirname+'/remaining-tests2';fs.mkdirSync(folder);");
const start=' const results=[];';assert.equal(value.split(start).length,2);
value=value.replace(start,` const npm=path.join(path.dirname(process.execPath),'node_modules/npm/bin/npm-cli.js');
 const install=await standard.capture(folder,'npm-ci',process.execPath,[npm,'--prefix',root+'/tests/acceptance','ci','--no-audit','--no-fund'],{
  runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:root,env,timeoutMs:180000,drainMs:2000});
 const installed=standard.verifyStream(install.rawReport,JSON.parse(install.contextBytes.toString('utf8')),install.ready,install.result);
 assert(install.result.exitCode===0&&install.result.supervisorExitCode===0&&installed.terminal.activeProcesses===0&&installed.terminal.closureConfirmed&&!installed.terminal.terminatedOwnedJob);
 const results=[];`);
fs.writeFileSync(__dirname+'/remaining-tests2.cjs',value,{flag:'wx'});
