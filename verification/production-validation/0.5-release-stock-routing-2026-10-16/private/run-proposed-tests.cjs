'use strict';
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module');
const root='C:/work/sm05-release-050';
const adapterFile=path.join(root,'tools/internal/release-verification.cjs');
const adapter=new Module(adapterFile,module);adapter.filename=adapterFile;adapter.paths=Module._nodeModulePaths(path.dirname(adapterFile));
adapter._compile(fs.readFileSync(__dirname+'/proposed/tools/internal/release-verification.cjs','utf8'),adapterFile);
const testFile=path.join(root,'tests/sdk/release-stock-routing.test.cjs');
const suite=new Module(testFile,module);suite.filename=testFile;suite.paths=Module._nodeModulePaths(path.dirname(testFile));
const originalRequire=suite.require.bind(suite),testFs=Object.create(fs);
testFs.readFileSync=(filename,...args)=>path.resolve(filename)===path.resolve(root,'tools/Collect-Verification.cjs')?
 fs.readFileSync(__dirname+'/proposed/tools/Collect-Verification.cjs',...args):fs.readFileSync(filename,...args);
suite.require=name=>name==='../../tools/internal/release-verification.cjs'?adapter.exports:name==='node:fs'?testFs:originalRequire(name);
suite._compile(fs.readFileSync(__dirname+'/proposed/tests/sdk/release-stock-routing.test.cjs','utf8'),testFile);
