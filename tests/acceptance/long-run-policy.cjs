'use strict';
const crypto=require('node:crypto');
// Root froze these new M0 choices before any actual long run. They are not
// inherited certification, the100reload rule, RTT or presentation budgets.
const HOUR=3600000,DURATION=8*HOUR;
const MODES=['reload','recreate','multiwindow','bridge','mixed','session'];
const RULE=Object.freeze({schema:'soui.long-run-acceptance-policy/v1',revision:'0.5.0-m0-software-cache-envelope-v4',
  durationMs:DURATION,hours:8,minSamplesPerHour:30,minActionsPerGroupPerHour:30,maxSampleGapMs:180000,
  percentile:'nearest-rank',privateMedianMinimumBytes:32*1024*1024,privateMedianPercent:20,
  privateP95MinimumBytes:64*1024*1024,privateP95Percent:25,
  hourlyMedianTheilSenUpper:0,hourlyP95TheilSenUpper:0,
  counterMedianGrowth:0,counterP95Growth:0,windowClasses:'exact-same-phase-every-point',
  actionMedianMinimumGrowthMs:25,actionP95MinimumGrowthMs:100,actionGrowthPercent:25,
  settleMs:500,timeoutMs:20000,mixedIdleMs:10000,sessionIdleMs:60000,
  ordinaryCycles:1000,ordinaryWarmup:0,workingSet:'descriptive-only',
  ownerCapacity:Object.freeze({engineQueued:580,engineExecuting:1,taskWorkers:2,taskInflight:32,taskSubscriptions:128,taskStore:1024,
    requestOrdinary:256,requestControl:128,requestRenewal:1,storedScopes:128,committedAliases:256,
    aliasTransientHighWater:257,moduleScopes:512,optionalGuiContexts:16,optionalGuiEffects:16}),
  ownerObservation:'independent-locks-fixed-per-lifetime-epochs-no-saturation',
  namedCacheObservation:'explicit-in-eight-hour-modes',
  namedCacheScope:'named-process-cache-scalars',
  namedCacheQualification:'data-only-original-native-qpc-independent-resource-budget',
  namedCacheConfigurationPolicy:require('./native-cache-validation.cjs').CONFIGURATION_POLICY,
  taskStoreRequestQuota:Object.freeze({maxLiveRequestedBytes:'134217728',scope:'task-store-private-request-byte-quota',
    coverage:'all-observed-live-and-retired-native-owners',finalDisposalObserved:false}),
  softwareCacheBudget:require('./software-cache-budget.cjs').POLICY,
  forcedGc:false,workingSetCompression:false,periodicProcessRestart:false,dropOutliers:false,
  incompleteFinalHour:'retain-separate-not-in-eight-hour-trend',releaseReady:false});
function fixedConfig(mode){
  if(!MODES.includes(mode))throw Error('Unsupported frozen long acceptance mode');
  return mode==='mixed'||mode==='session'?
    {mode,durationMs:DURATION,idleMs:mode==='session'?60000:10000,settleMs:500,timeoutMs:20000}:
    {mode,cycles:1000,warmup:0,settleMs:500,timeoutMs:20000};
}
function policyBytes(mode){
  const config=fixedConfig(mode);
  return Buffer.from(JSON.stringify({...RULE,mode,config,
    warmupEnd:mode==='session'?'authenticated-started-and-initial-native-anchor-sample':
      mode==='mixed'?'zero-warmup-cold-start-retained':'zero-warmup',
    coverage:mode==='session'?'same-document-task-subscription-renewal':
      mode==='mixed'?'mixed-lifecycle-business':'same-process-lifecycle'},null,2)+'\n');
}
const hash=raw=>crypto.createHash('sha256').update(raw).digest('hex');
function captureScope(config){return {kernel:'miniblink',renderer:'software',processModel:'single-process',mode:config.mode,
  namedCacheObservation:config.durationMs===undefined?'not-requested':RULE.namedCacheScope};}
function sameConfig(a,b){return !!a&&typeof a==='object'&&!Array.isArray(a)&&Object.keys(a).sort().join('|')===Object.keys(b).sort().join('|')&&Object.keys(b).every(key=>a[key]===b[key]);}
function policySelection(config){
  const expected=fixedConfig(config?.mode);
  if(!sameConfig(config,expected))throw Error('Long capture requires the exact Root-frozen current settings');
  const policyRaw=policyBytes(config.mode);
  return {policyRaw,policyFile:'long-run-policy.json',
    requestFields:{longRunPolicySha256:hash(policyRaw),
      longRunScope:captureScope(config)}};
}
function validateFrozenRequest(record,build,runRoot){
  const config=fixedConfig(record?.mode);
  const allowed=['schema','runId',...Object.keys(config),'longRunPolicySha256','longRunScope','consumerBuildId','payloadSha256','sourceSha256','startedAt'];
  if(!record||Object.keys(record).sort().join('|')!==allowed.sort().join('|'))throw Error('Long request fields differ from the fixed contract');
  const path=require('node:path');
  if(record.schema!=='soui.acceptance-request/v1'||typeof record.runId!=='string'||
    !/^[a-f0-9]{8}-[a-f0-9]{4}-[1-5][a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$/.test(record.runId)||record.consumerBuildId!==build.consumerBuildId||
    record.sourceSha256!==build.sourceSha256||record.payloadSha256!==build.payload.sha256||
    record.longRunPolicySha256!==hash(policyBytes(record.mode))||record.resourcePolicySha256!==undefined||
    !sameConfig(record.longRunScope,captureScope(config))||
    Object.entries(config).some(([key,value])=>record[key]!==value)||
    (config.durationMs===undefined ? record.durationMs!==undefined||record.idleMs!==undefined : record.cycles!==undefined||record.warmup!==undefined)||
    path.basename(runRoot)!==record.mode+'-'+record.runId)throw Error('Frozen current long acceptance request differs');
  return config;
}
module.exports={RULE,HOUR,DURATION,MODES,fixedConfig,policyBytes,policySelection,validateFrozenRequest,sameConfig,hash};
