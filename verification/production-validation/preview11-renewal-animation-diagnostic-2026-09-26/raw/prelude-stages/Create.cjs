'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const from='C:/SOUI-SDK-production-validation/preview11-renewal-page-stages-preparation-20260926',root=__dirname;
let text=fs.readFileSync(path.join(from,'Prepare.cjs'),'utf8');
const previous="html=replaceOnce(html,prelude,marker('prelude-before')+prelude+marker('prelude-after'));";
const replacement=`const instrumented=prelude
  .replace('try{var D=window.DOMException;new D(1,2);}', 'try{window.__souiStage("exception-read-before");var D=window.DOMException;window.__souiStage("exception-read-after "+typeof D);window.__souiStage("exception-new-before");new D(1,2);window.__souiStage("exception-new-after");}')
  .replace('catch(e){var P=D&&D.prototype;', 'catch(e){window.__souiStage("exception-catch");var P=D&&D.prototype;window.__souiStage("exception-prototype-read");')
  .replace('if(P)S.prototype=P;try{window.DOMException=S;}catch(ignore){}', 'window.__souiStage("exception-prototype-set-before");if(P)S.prototype=P;window.__souiStage("exception-prototype-set-after");try{window.__souiStage("exception-global-set-before");window.DOMException=S;window.__souiStage("exception-global-set-after");}catch(ignore){window.__souiStage("exception-global-set-catch");}');
 html=replaceOnce(html,prelude,marker('prelude-before')+instrumented+marker('prelude-after'));`;
assert.equal(text.split(previous).length,2);text=text.replace(previous,replacement);
fs.writeFileSync(path.join(root,'Prepare.cjs'),text,{flag:'wx'});
for(const name of ['Run.cjs','Capture-Command.cjs'])fs.copyFileSync(path.join(from,name),path.join(root,name),fs.constants.COPYFILE_EXCL);
