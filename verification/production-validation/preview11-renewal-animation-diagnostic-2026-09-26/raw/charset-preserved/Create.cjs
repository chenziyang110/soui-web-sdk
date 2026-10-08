'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const from='C:/SOUI-SDK-production-validation/preview11-renewal-prelude-stages-preparation-20260926',root=__dirname;
let script=fs.readFileSync(path.join(from,'Prepare.cjs'),'utf8');
const previous="let html=replaceOnce(before,'<head>','<head>\\n<script>'+observer+'</script>');";
const replacement="let html=replaceOnce(before,'<meta charset=\"UTF-8\" />','<meta charset=\"UTF-8\" />\\n<script>'+observer+'</script>');";
assert.equal(script.split(previous).length,2);script=script.replace(previous,replacement);
fs.writeFileSync(path.join(root,'Prepare.cjs'),script,{flag:'wx'});
for(const name of ['Run.cjs','Capture-Command.cjs'])fs.copyFileSync(path.join(from,name),path.join(root,name),fs.constants.COPYFILE_EXCL);
