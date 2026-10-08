'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const parent='C:/SOUI-SDK-production-validation/preview11-renewal-parser-state-preparation-20260926';
let prepare=fs.readFileSync(path.join(parent,'Prepare.cjs'),'utf8');
const needle='  setTimeout(inspect,20000);';assert.equal(prepare.split(needle).length,2);
prepare=prepare.replace(needle,needle+'\n  setTimeout(function(){window.__souiStage("animation-stop-before state="+document.readyState);var spinner=document.querySelector(".spinner");if(spinner)spinner.style.animation="none";window.__souiStage("animation-stop-after state="+document.readyState);},15000);');
for(const name of ['Run.cjs','Capture-Command.cjs'])fs.copyFileSync(path.join(parent,name),path.join(__dirname,name),fs.constants.COPYFILE_EXCL);
fs.writeFileSync(path.join(__dirname,'Prepare.cjs'),prepare,{flag:'wx'});
fs.writeFileSync(path.join(__dirname,'hypothesis.json'),JSON.stringify({hypothesis:'Keep the original infinite animation and parser-state observer. Stop only that spinner at 15 seconds. Observe whether the already-stalled document completes parsing immediately after the controlled stop, within the unchanged initial 60-second native deadline. This tests causality inside one process; no production fix or true renewal claim.',parent,productionPass:false},null,2)+'\n',{flag:'wx'});
