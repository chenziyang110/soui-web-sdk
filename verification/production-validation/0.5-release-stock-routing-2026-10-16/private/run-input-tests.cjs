'use strict';
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module');
const root='C:/work/sm05-release-050',proposed=process.env.INSTALLED_INPUTS==='proposed';
const sdkFile=path.join(root,'tools/sdk/soui-sdk.cjs');
const sdk=new Module(sdkFile,module);sdk.filename=sdkFile;sdk.paths=Module._nodeModulePaths(path.dirname(sdkFile));
sdk._compile(fs.readFileSync(proposed?__dirname+'/proposed/tools/sdk/soui-sdk.cjs':sdkFile,'utf8'),sdkFile);
const inputs=['cmake/toolchains/SouiLLVM22.cmake','tools/internal/Invoke-ToolFileIdentity.ps1','tools/internal/Tool-FileIdentity.ps1'];
const testFs=Object.create(fs);
testFs.readFileSync=(file,...args)=>{
 const relative=path.relative(root,file).split(path.sep).join('/');
 return fs.readFileSync(proposed&&inputs.includes(relative)?__dirname+'/proposed/'+relative:file,...args);
};
const filename=path.join(root,'tests/sdk/sdk-build-inputs.test.cjs');
const suite=new Module(filename,module);suite.filename=filename;suite.paths=Module._nodeModulePaths(path.dirname(filename));
const requireOriginal=suite.require.bind(suite);
suite.require=name=>name==='../../tools/sdk/soui-sdk.cjs'?sdk.exports:name==='node:fs'?testFs:requireOriginal(name);
suite._compile(fs.readFileSync(__dirname+'/proposed/tests/sdk/sdk-build-inputs.test.cjs','utf8'),filename);
if(proposed){
 const packageTest=path.join(root,'tests/sdk/sdk-package-smoke.cjs'),smoke=new Module(packageTest,module);
 smoke.filename=packageTest;smoke.paths=Module._nodeModulePaths(path.dirname(packageTest));const before=smoke.require.bind(smoke);
 smoke.require=name=>name==='../../tools/sdk/soui-sdk.cjs'?sdk.exports:before(name);
 smoke._compile(fs.readFileSync(packageTest,'utf8'),packageTest);
}
