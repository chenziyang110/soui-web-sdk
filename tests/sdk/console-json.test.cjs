'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {consoleJson}=require('../../tools/internal/build-evidence.cjs');
test('console JSON survives native code-page conversion with exact Unicode paths and values',()=>{
 const value={files:[{path:'resources/配置.ini',bytes:6},{path:'é/😀/e\u0301.txt',bytes:0}],quote:'"\\\r\n',value:123};
 const text=consoleJson(value);
 assert.equal(/[^\x00-\x7f]/.test(text),false);
 assert.deepEqual(JSON.parse(Buffer.from(text,'ascii').toString('latin1')),value);
 assert.deepEqual(JSON.parse(text),value);
});
