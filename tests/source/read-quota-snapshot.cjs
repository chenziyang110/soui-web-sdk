'use strict';
// Pure specification reader for a separate future native sample. Models do not
// represent an actual emitted record or production calibration receipt.
const assert=require('node:assert/strict');
const names=['schema','status','maxLiveRequestedBytes','ownerEpoch','ownerEpochValid','chargedLiveRequestedBytes','chargedPeakRequestedBytes','successfulLiveRequestedBytes','successfulPeakRequestedBytes','deniedRequests','deniedRequestsExact','creditValid'];
const maximum=(1n<<64n)-1n;
function u64(value){assert.equal(typeof value,'string');assert(/^(?:0|[1-9][0-9]{0,19})$/.test(value));const n=BigInt(value);assert(n<=maximum);return n;}
function readQuotaSample(value,expectedPolicy){
  assert(value&&typeof value==='object'&&!Array.isArray(value));assert.equal(Object.keys(value).length,names.length);for(const n of names)assert(Object.hasOwn(value,n));
  assert.equal(value.schema,'soui.native-task-store-request-quota/1');assert(['available','busy','invalid','unavailable'].includes(value.status));
  for(const n of ['ownerEpochValid','deniedRequestsExact','creditValid'])assert.equal(typeof value[n],'boolean');
  const cap=value.maxLiveRequestedBytes===null?null:u64(value.maxLiveRequestedBytes),expected=expectedPolicy===null?null:u64(expectedPolicy);
  if(value.status==='available'||value.status==='invalid')assert.equal(cap,expected,'Caller policy mismatch');
  const epoch=u64(value.ownerEpoch),live=u64(value.chargedLiveRequestedBytes),peak=u64(value.chargedPeakRequestedBytes),success=u64(value.successfulLiveRequestedBytes),successfulPeak=u64(value.successfulPeakRequestedBytes),denied=u64(value.deniedRequests);
  assert.equal(value.ownerEpochValid,epoch>0n,'Epoch validity does not match identity');
  assert(peak>=live&&live>=success&&successfulPeak>=success&&peak>=successfulPeak);
  if(value.status==='available'){assert(value.ownerEpochValid&&epoch>0n&&value.creditValid&&value.deniedRequestsExact);assert(cap===null||live<=cap);}
  else if(value.status==='busy'||value.status==='unavailable'){assert(!value.ownerEpochValid&&!value.creditValid&&!value.deniedRequestsExact&&epoch===0n&&live===0n&&peak===0n&&success===0n&&successfulPeak===0n&&denied===0n);assert.equal(cap,null);}
  else {assert(!value.creditValid||!value.ownerEpochValid||!value.deniedRequestsExact);if(!value.deniedRequestsExact)assert.equal(denied,maximum,'Inexact refusal counter must remain saturated');}
  return {state:value.status,policyKind:value.status==='busy'||value.status==='unavailable'?'unobserved':cap===null?'unconfigured':cap===0n?'zero':'finite',snapshotAvailable:value.status==='available',reservedRequestedBytes:live-success,deniedRequests:denied,productionDefaultGate:'NOT_EVALUATED',calibrationPin:null,fullCacheBudgetGate:'NOT_EVALUATED',releaseReady:false};
}
module.exports={readQuotaSample,u64};
