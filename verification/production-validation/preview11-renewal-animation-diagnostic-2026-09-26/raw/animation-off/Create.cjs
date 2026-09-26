'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const parent='C:/SOUI-SDK-production-validation/preview11-renewal-parser-state-preparation-20260926';
let prepare=fs.readFileSync(path.join(parent,'Prepare.cjs'),'utf8');
const needle=' const scripts=[...html.matchAll';assert.equal(prepare.split(needle).length,2);
prepare=prepare.replace(needle,' html=replaceOnce(html,"animation:turn 1s linear infinite;","animation:none;");\n'+needle);
for(const name of ['Run.cjs','Capture-Command.cjs'])fs.copyFileSync(path.join(parent,name),path.join(__dirname,name),fs.constants.COPYFILE_EXCL);
fs.writeFileSync(path.join(__dirname,'Prepare.cjs'),prepare,{flag:'wx'});
fs.writeFileSync(path.join(__dirname,'hypothesis.json'),JSON.stringify({hypothesis:'Continuous CSS animation may keep default-priority work ready and starve lower-priority parser loading tasks. Disable only the controlled spinner animation relative to parser-state diagnostic; preserve native binary, JS and deadlines. A changed result would narrow the cause, not prove a production fix.',parent,productionPass:false},null,2)+'\n',{flag:'wx'});
