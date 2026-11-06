'use strict';
// Static source-equivalence counterexamples only. This executes no Win32 API,
// fake WinAPI implementation, target input, compiler or GUI.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),test=require('node:test');
const root=path.dirname(__dirname),source=fs.readFileSync(path.join(root,'project/input.cpp'),'utf8'),old=fs.readFileSync(path.resolve(root,'../diagnostic-source-r41/project/input.cpp'),'utf8');
const begin='void validate(const target&t,bool foreground=true)',hot=s=>s.slice(s.indexOf(begin),s.indexOf('\nstruct count_windows',s.indexOf(begin))).split('\nvoid cold_validate')[0].trim();
const expected=hot(old),body=expected.slice(expected.indexOf('{')),cold=source.split('// COLD_ORIGINAL_BODY_BEGIN')[1].split('// COLD_ORIGINAL_BODY_END')[0].trim(),normal=s=>s.replace(/\s/g,'');
function unwrap(s){let count=0;for(;;){const m=/guard_term\("[^"]+",\(/.exec(s);if(!m)return{text:s,count};const open=m.index+m[0].length-1;let depth=1,q=open+1;for(;q<s.length&&depth;q++){if(s[q]==='(')depth++;else if(s[q]===')')depth--;}assert.equal(s[q],')','Only observation wrapper may be removed');s=s.slice(0,m.index)+s.slice(open+1,q-1)+s.slice(q+1);count++;}}
function check(s){const a=unwrap(s);assert.equal(a.count,38,'All original named predicates observed');assert.equal(normal(a.text),normal(body),'Cold predicate/API order changed');}
test('actual hot block is byte-exact and actual cold observations normalize to the same38 original predicates',()=>{assert.equal(hot(source),expected);check(cold);assert(source.includes('const auto zero=capture(t,&cold);'));assert(source.includes('final=capture(t);'));assert(source.includes('wait_deadline(timer.h,process.h,r.scheduled,frequency);validate(t);'));});
test('static checker rejects deleting a real owner-height predicate rather than blessing a shorter guard',()=>{check(cold);const bad=cold.replace('guard_term("owner-height",(o.bottom==792))','true');assert.notEqual(bad,cold);assert.throws(()=>check(bad),/predicates|order/);});
test('static checker rejects swapping the actual owner and strip RECT query order',()=>{check(cold);const bad=cold.replace('GetClientRect(t.owner,&o)','R42_SWAP').replace('GetClientRect(t.strip,&s)','GetClientRect(t.owner,&o)').replace('R42_SWAP','GetClientRect(t.strip,&s)');assert.notEqual(bad,cold);assert.throws(()=>check(bad),/order/);});
