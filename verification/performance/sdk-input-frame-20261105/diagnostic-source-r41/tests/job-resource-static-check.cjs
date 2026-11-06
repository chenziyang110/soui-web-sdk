'use strict';
// Static source guard only. It never calls Win32 and cannot prove successful
// job accounting, application attribution, CPU completeness or actual bytes.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),header=fs.readFileSync(path.join(root,'project/job_resources.h'),'utf8'),main=fs.readFileSync(path.join(root,'project/main.cpp'),'utf8');
assert.equal((header.match(/QueryInformationJobObject\(nullptr,/g)||[]).length,2);
for(const kind of['BasicAccounting','ExtendedLimit'])assert(header.includes('JobObject'+kind+'Information'));
assert(!/CreateThread|CreateJobObject|SetInformationJobObject|Sleep\(|WaitFor|GetProcessMemoryInfo/.test(header));
for(const status of['basic','extended'])assert(header.includes('if(!s.'+status+'.succeeded)s.'+status+'.error=GetLastError()'));
assert(header.includes('s.attempted&&!s.succeeded?json(s.error):json(nullptr)'));
for(const field of['TotalUserTime100ns','TotalKernelTime100ns','TotalProcesses','ActiveProcesses','TotalTerminatedProcesses']){
 const line=header.split('\n').find(line=>line.includes('basic["'+field+'"]='));assert(line&&line.includes('=b?json(')&&line.endsWith(':json(nullptr);'));
}
assert(header.includes('extended["PeakJobMemoryUsedBytes"]=e?json(std::to_string('));
assert(header.includes('s.begin_known?json(std::to_string(s.begin)):json(nullptr)')&&header.includes('s.end_known?json(std::to_string(s.end)):json(nullptr)'));
assert.equal((main.match(/query_immediate_job_resources\(\)/g)||[]).length,1);
assert(main.includes('if(next==2||next==3)job_resource_boundaries[next-2]=owner_fairness::query_immediate_job_resources();'));
assert(main.includes('{"fullApplicationAttributionVerified",false},{"jobCpuIntervalCompletenessVerified",false}'));
console.log(JSON.stringify({schema:'soui.r41-job-resource-static-check/v1',staticSourceChecksPassed:true,Win32Executed:false,compilerExecuted:false,CPUCompletenessVerified:false}));
