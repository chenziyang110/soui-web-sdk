'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');const work=__dirname;
const test=work+'/proposal/tests/native/windows_uia_provider_tests.cpp';let s=fs.readFileSync(test,'utf8');
assert(!s.includes('class tree_events final'));
s=s.replace('void com_initialization_case()',fs.readFileSync(work+'/events-extra.inc','utf8')+'\nvoid com_initialization_case()');
s=s.replace('{"property-events", property_events_case},','{"property-events", property_events_case}, {"tree-events", tree_events_case}, {"revoked-events", revoked_events_case},');
fs.writeFileSync(test,s);
const runner=work+'/run.cjs';let r=fs.readFileSync(runner,'utf8');
r=r.replace("await run('events',exe,[selected],expected);",`if(selected==='each') {
   for(const name of ['properties','mutations','stale-secret','bounded','callback-close','uia-client','com-initialization','api-close','property-events','tree-events','revoked-events'])
     await run('case-'+name,exe,[name],expected);
 } else await run('events',exe,[selected],expected);`);
fs.writeFileSync(runner,r);
