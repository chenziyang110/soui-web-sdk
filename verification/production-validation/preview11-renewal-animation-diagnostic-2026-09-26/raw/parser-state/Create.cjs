'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const from='C:/SOUI-SDK-production-validation/preview11-renewal-charset-preserved-preparation-20260926',root=__dirname;
let script=fs.readFileSync(path.join(from,'Prepare.cjs'),'utf8');
const previous='setTimeout(function(){window.__souiStage("timer-1000 state="+document.readyState);},1000);';
const replacement=`function inspect(){
    var scripts=document.getElementsByTagName("script"),details=document.getElementById("details");
    window.__souiStage("parser state="+document.readyState+" scripts="+scripts.length+" details="+(details?"present":"absent"));
    window.__souiStage("globals System="+typeof window.System+" native="+typeof window.__souiNativePostMessage+" exception="+typeof window.DOMException);
    for(var i=0;i<scripts.length;i++){var s=scripts[i];window.__souiStage("script "+i+" type="+(s.type||"classic")+" src="+(s.src?s.src.split("/").pop():"inline")+" text="+(s.text||"").length);}
  }
  setTimeout(inspect,1000);
  setTimeout(inspect,20000);`;
assert.equal(script.split(previous).length,2);script=script.replace(previous,replacement);
fs.writeFileSync(path.join(root,'Prepare.cjs'),script,{flag:'wx'});
for(const name of ['Run.cjs','Capture-Command.cjs'])fs.copyFileSync(path.join(from,name),path.join(root,name),fs.constants.COPYFILE_EXCL);
