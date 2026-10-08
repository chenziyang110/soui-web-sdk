'use strict';
const crypto = require('node:crypto');
const path = require('node:path');
const {isUtf8} = require('node:buffer');
const SCHEMA = 'soui.owned-tail-snapshot/v1';
const UINT64 = 18446744073709551615n;
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function parseStrictJson(text) {
  const value=JSON.parse(text); let cursor=0;
  const white=()=>{while(/\s/.test(text[cursor]||'!'))cursor++;};
  function string(){const begin=cursor++;while(cursor<text.length){if(text[cursor]==='\\'){cursor+=2;continue;}if(text[cursor++]==='"')return JSON.parse(text.slice(begin,cursor));}throw new Error('Unterminated JSON string');}
  function scan(depth=0){
    requireValue(depth<=128,'Excessive JSON nesting');white();const token=text[cursor];
    if(token==='"'){string();return;}
    if(token==='{'||token==='['){
      const object=token==='{',close=object?'}':']',keys=new Set();cursor++;white();
      if(text[cursor]===close){cursor++;return;}
      while(true){white();if(object){const key=string();requireValue(!keys.has(key),'Duplicate JSON key');keys.add(key);white();cursor++;}scan(depth+1);white();if(text[cursor++]===close)return;}
    }
    while(cursor<text.length&&!/[\s,}\]]/.test(text[cursor]))cursor++;
  }
  scan();white();requireValue(cursor===text.length,'Unexpected JSON suffix');return value;
}
function requireValue(condition, message) { if (!condition) throw new Error(message); }
function decimal(value, positive = false, maximum = UINT64) {
  requireValue(typeof value === 'string' && /^(0|[1-9]\d*)$/.test(value) && value.length <= 20, 'Invalid uint64 string');
  const number = BigInt(value);
  requireValue(number <= maximum && (!positive || number > 0n), 'uint64 out of bounds');
  return number;
}
function count(value, positive = false) {
  requireValue(Number.isSafeInteger(value) && value >= (positive ? 1 : 0) && value <= 4294967295, 'Invalid DWORD count');
  return value;
}
function fields(value, required, optional = []) {
  requireValue(required.every(key => Object.hasOwn(value,key)) &&
    Object.keys(value).every(key => required.includes(key) || optional.includes(key)), 'Invalid generic observation fields');
}
function canonicalImage(value) {
  requireValue(typeof value === 'string' && /^[A-Za-z]:[\\/]/.test(value) && !/[\x00-\x1f<>"|?*]/.test(value), 'Invalid pin image path');
  const normalized = value.replace(/\//g, '\\');
  const pieces = normalized.slice(3).split('\\');
  requireValue(pieces.length > 0 && pieces.every(piece => piece && piece !== '.' && piece !== '..' && !/[.: ]$/.test(piece) && !piece.includes(':')),
    'Aliased pin image path');
  requireValue(path.win32.normalize(normalized) === normalized, 'Noncanonical pin image path');
  return normalized;
}
function validateTailImagePin(pin) {
  requireValue(pin && typeof pin === 'object' && !Array.isArray(pin) && Object.keys(pin).sort().join(',') === 'image,nonce,sha256', 'Complete tail image pin required');
  canonicalImage(pin.image);
  requireValue(typeof pin.sha256 === 'string' && /^[a-f0-9]{64}$/.test(pin.sha256), 'Invalid tail pin SHA256');
  requireValue(typeof pin.nonce === 'string' && /^[a-f0-9]{32}$/.test(pin.nonce), 'Invalid tail snapshot nonce');
  return {...pin};
}
const diskFields = ['canonicalPath','volumePath','volumeSerialNumber','fileId','size','sha256'];
function disk(value) {
  requireValue(value && typeof value === 'object', 'Missing disk object');
  requireValue(typeof value.canonicalPath === 'string' && value.canonicalPath.startsWith('\\\\?\\'), 'Invalid canonical disk path');
  canonicalImage(value.canonicalPath.slice(4));
  requireValue(typeof value.volumePath === 'string' && /^\\\\\?\\Volume\{[a-fA-F0-9]{8}-(?:[a-fA-F0-9]{4}-){3}[a-fA-F0-9]{12}\}\\/.test(value.volumePath), 'Invalid volume GUID path');
  const volumeSuffix = value.volumePath.slice(value.volumePath.indexOf('}') + 2);
  requireValue(volumeSuffix === value.canonicalPath.slice(7), 'Volume and drive paths differ');
  decimal(value.volumeSerialNumber, false, 4294967295n); decimal(value.fileId, true);
  decimal(value.size, true, 67108864n);
  requireValue(typeof value.sha256 === 'string' && /^[a-f0-9]{64}$/.test(value.sha256), 'Invalid disk SHA256');
  return Object.fromEntries(diskFields.map(field => [field,value[field]]));
}
function equalDisk(a, b) { return diskFields.every(field => a[field] === b[field]); }
function accounting(value) {
  requireValue(value && typeof value === 'object', 'Missing Job accounting');
  count(value.activeProcesses); count(value.totalProcesses, true); count(value.limitTerminatedProcesses);
  requireValue(value.activeProcesses <= value.totalProcesses && value.limitTerminatedProcesses === 0, 'Invalid or limit-terminated Job accounting');
  return value;
}
function pids(value) {
  requireValue(Array.isArray(value) && value.length <= 8192, 'Invalid PID list');
  value.forEach(pid => count(pid,true));
  requireValue(value.every((pid,index) => index === 0 || pid > value[index-1]), 'Duplicate or unsorted PID list');
  return value;
}
const equalPids = (a,b) => a.length === b.length && a.every((pid,index) => pid === b[index]);

// Reads actual raw bytes, not a caller-supplied "complete" boolean. This proves
// consistency of recorded disk/Job observations, not a trusted writer or RAM.
function readTailEvidence(raw, {context, reportSha256} = {}) {
  const rejected = (failure, available = true) => ({schema:'soui.owned-tail-verdict/v1', available, accepted:false,
    failure, diskBytesOnly:true, reportSha256: Buffer.isBuffer(raw) ? hash(raw) : null});
  if (!context?.tailImagePin) return rejected('No prelaunch tail pin context', false);
  try {
    const option = validateTailImagePin(context.tailImagePin);
    requireValue(Number.isSafeInteger(context.timeoutMs) && context.timeoutMs >= 1 && context.timeoutMs <= 172800000 &&
      Number.isSafeInteger(context.drainMs) && context.drainMs >= 0 && context.drainMs <= 60000 &&
      Array.isArray(context.args) && context.args.every(arg => typeof arg === 'string'), 'Invalid pin context budgets/argv');
    canonicalImage(context.executable); canonicalImage(context.cwd); canonicalImage(context.report);
    const argv=['--timeout-ms',String(context.timeoutMs),'--drain-ms',String(context.drainMs),'--stop-on-stdin','--report',context.report,
      '--cwd',context.cwd,'--pin-tail-image',option.image,'--pin-tail-sha256',option.sha256,'--snapshot-nonce',option.nonce,
      '--',context.executable,...context.args];
    requireValue(Array.isArray(context.runnerArgs) && context.runnerArgs.length === argv.length && context.runnerArgs.every((arg,index) => arg === argv[index]),
      'Actual supervisor argv differs from pin context');
    requireValue(Buffer.isBuffer(raw) && raw.length > 0 && raw.length <= 16777216 && isUtf8(raw) && raw.at(-1) === 10, 'Incomplete or invalid owned stream bytes');
    const actualHash = hash(raw);
    requireValue(/^[a-f0-9]{64}$/.test(reportSha256 || '') && reportSha256 === actualHash, 'Raw report SHA256 differs');
    const lines = raw.toString('utf8').slice(0,-1).split('\n');
    requireValue(lines.length <= 100000 && lines.every(line => line.trim()), 'Blank or excessive owned stream lines');
    const rows = lines.map(parseStrictJson);
    requireValue(rows.every(row => row && typeof row === 'object' && !Array.isArray(row)), 'Invalid stream record');
    const start = rows[0]; const terminal = rows.at(-1);
    requireValue(start.schema === 'soui.owned-process-run/v1' && start.event === 'start' && start.jobAssignedBeforeResume === true &&
      start.breakawayAllowed === false && start.snapshotNonce === option.nonce, 'Invalid pinned start identity');
    count(start.pid,true); decimal(start.creationFileTime,true);
    requireValue(start.timeoutMs === context.timeoutMs && start.drainMs === context.drainMs, 'Start budgets differ from context');
    requireValue(terminal.event === 'terminal' && terminal.reason === 'owned-descendants-remain' && terminal.rootExited === true &&
      terminal.rootExitCode === 0 && terminal.passed === false && terminal.activeBeforeCleanup === 1 && terminal.terminatedOwnedJob === true &&
      terminal.closureConfirmed === true && terminal.activeProcesses === 0 && terminal.limitTerminatedProcesses === 0 && terminal.tailEvidenceComplete === true,
      'Generic terminal is not an eligible forced tail cleanup');
    count(terminal.elapsedMs); count(terminal.totalProcesses,true);
    const stream = []; const genericMembers = []; let snapshotBegan = false; let rootExit = null;
    const identities = new Map([[start.pid,start.creationFileTime]]);
    const observeIdentity = (pid, creation) => {
      count(pid,true); decimal(creation,true);
      requireValue(!identities.has(pid) || identities.get(pid) === creation, 'PID reused with a different FILETIME');
      identities.set(pid,creation);
    };
    for (let index=1;index<rows.length-1;index++) {
      const row = rows[index];
      if (row.schema === SCHEMA) {
        requireValue(row.nonce === option.nonce && row.sequence === stream.length+1, 'Snapshot nonce or sequence differs');
        count(row.attempt);
        requireValue(!row.error && row.stickyFailure !== true, 'Sticky snapshot failure cannot be retried');
        if (row.event !== 'pin') snapshotBegan = true;
        stream.push(row); continue;
      }
      requireValue(!row.schema && !snapshotBegan, 'Unknown schema or generic event after snapshot began');
      if (row.event === 'owned-member') {
        fields(row,['event','pid','creationFileTime','image'],['imageReadError']);
        count(row.pid,true); decimal(row.creationFileTime,true);
        if (typeof row.image === 'string') {
          requireValue(!Object.hasOwn(row,'imageReadError'), 'Known generic image carries a read error');
          canonicalImage(row.image);
        } else {
          requireValue(row.image === null && Object.hasOwn(row,'imageReadError'), 'Unknown generic member image');
          count(row.imageReadError,true);
        }
        genericMembers.push({row,beforeRoot:rootExit === null});
      } else if (row.event === 'member-unavailable') {
        fields(row,['event','pid','win32Error'],['operation']); count(row.pid,true); count(row.win32Error,true);
        requireValue(!Object.hasOwn(row,'operation') || ['verify-membership','read-identity'].includes(row.operation), 'Unknown unavailable member operation');
        genericMembers.push({row,beforeRoot:rootExit === null});
      } else if (row.event === 'member-no-longer-owned') {
        fields(row,['event','pid']); count(row.pid,true);
        genericMembers.push({row,beforeRoot:rootExit === null});
      } else if (row.event === 'job-members') {
        fields(row,['event','elapsedMs','activeProcesses','totalProcesses','limitTerminatedProcesses']);
        accounting(row); count(row.elapsedMs);
        requireValue(row.totalProcesses <= terminal.totalProcesses, 'Job total exceeds final fixed total');
      } else if (row.event === 'root-exit') {
        fields(row,['event','elapsedMs','exitCode']);
        requireValue(rootExit === null && row.exitCode === 0, 'Missing or duplicate successful root exit');
        count(row.elapsedMs); rootExit = row;
      } else throw new Error('Unknown or unavailable generic enumeration');
    }
    requireValue(rootExit && rootExit.elapsedMs + context.drainMs <= terminal.elapsedMs && terminal.elapsedMs < context.timeoutMs,
      'Root exit/drain/timeout timing is inconsistent');
    let cursor=0;
    function take(event,phase,attempt) {
      const row=stream[cursor++];
      requireValue(row && row.event === event && row.phase === phase && row.attempt === attempt, 'Unexpected, missing or duplicated '+event);
      return row;
    }
    const pin = take('pin','prelaunch',0); const pinnedDisk = disk(pin);
    requireValue(rows[1] === pin && pin.sequence === 1 && pin.sha256 === option.sha256 &&
      pin.canonicalPath.slice(4).toLowerCase() === canonicalImage(option.image).toLowerCase() && pin.diskBytesOnly === true &&
      pin.pinBudgetMs === 2000 && pin.snapshotBudgetMs === 1000 && pin.maximumFileBytes === '67108864', 'Pin/context/budgets differ');
    requireValue(decimal(pin.observedBeforeLaunchUtcFileTime,true) <= decimal(start.creationFileTime,true), 'Pin was not observed before launch');
    decimal(pin.observedBeforeLaunchTickMs);
    const fixedTotal = terminal.totalProcesses;
    requireValue(fixedTotal >= 2, 'Tail and exited root require at least two assigned processes');
    function total(value) { accounting(value); requireValue(value.totalProcesses === fixedTotal, 'Snapshot fixed Job total changed'); return value; }
    function list(pass,attempt) {
      const phase = pass === 'final' ? 'pre-cleanup-confirm' : 'pre-cleanup';
      let lastCapacity = 0;
      for (let growth=0;growth<=4;growth++) {
        const row = take('snapshot.list',phase,attempt);
        count(row.capacity,true); count(row.assigned); count(row.listed); count(row.win32Error);
        requireValue(row.pass === pass && row.growth === growth && row.capacity > lastCapacity && row.capacity <= 8191 &&
          row.listed <= row.capacity && row.listed <= row.assigned, 'Invalid PID list capacity/growth/counts');
        requireValue(row.win32Error === 0 || row.win32Error === 234, 'PID enumeration failed');
        if (row.win32Error === 0 && row.listed === row.assigned) return row;
        requireValue(growth < 4, 'PID list never completed'); lastCapacity = row.capacity;
      }
      throw new Error('PID list growth budget exceeded');
    }
    let selected=null;
    for (let attempt=1;attempt<=3;attempt++) {
      const begin=take('snapshot.begin','pre-cleanup',attempt); const before=total(begin.accountingBefore);
      const first=list('first',attempt); const members=[];
      while (stream[cursor]?.event === 'snapshot.member') {
        const member=take('snapshot.member','pre-cleanup',attempt);
        requireValue(member.index === members.length && member.inHeldJob === true && typeof member.alive === 'boolean' && member.pinMatched === true,
          'Unknown disk member or member index hole');
        canonicalImage(member.image); observeIdentity(member.pid,member.creationFileTime);
        requireValue(equalDisk(disk(member.diskObject),pinnedDisk) && member.image.toLowerCase() === pin.canonicalPath.slice(4).toLowerCase(), 'Member disk/path differs from pin');
        members.push(member);
      }
      const second=list('second',attempt); const end=take('snapshot.end','pre-cleanup',attempt); const after=total(end.accountingAfter);
      const firstPids=pids(end.pids); const secondPids=pids(end.secondPids);
      requireValue(end.stickyFailure === false && typeof end.complete === 'boolean' && typeof end.eligible === 'boolean' &&
        end.assigned === first.assigned && end.listed === first.listed && end.uniquePids === firstPids.length &&
        end.members === members.length && firstPids.length === first.listed && secondPids.length === second.listed &&
        equalPids(firstPids,members.map(member=>member.pid)), 'Snapshot counts or held identity set differs');
      const stable = before.activeProcesses === first.assigned && first.assigned === first.listed && first.listed === members.length &&
        members.length === after.activeProcesses && second.assigned === after.activeProcesses && equalPids(firstPids,secondPids) && members.every(member=>member.alive);
      const eligible=stable && members.length === 1;
      requireValue(end.complete === stable && end.eligible === eligible, 'Snapshot complete/eligible contradicts observations');
      if (!stable) continue;
      requireValue(eligible, 'Multiple or zero stable tails are not eligible');
      const final=list('final',attempt); const confirmation=take('pre-cleanup-confirm','pre-cleanup-confirm',attempt);
      const finalAccount=total(confirmation.accounting); const finalPids=pids(confirmation.pids);
      requireValue(confirmation.snapshotSequence === end.sequence && confirmation.identitiesMatch === true &&
        typeof confirmation.confirmed === 'boolean' && finalPids.length === final.listed, 'Invalid final pre-cleanup reference');
      const confirmed=confirmation.identitiesMatch && final.assigned === 1 && final.listed === 1 && finalAccount.activeProcesses === 1 && equalPids(finalPids,firstPids);
      requireValue(confirmation.confirmed === confirmed, 'Final confirmation contradicts observations');
      if (!confirmed) continue;
      selected={end,member:members[0]}; break;
    }
    requireValue(selected, 'No decisive pre-cleanup candidate');
    const sequence=selected.end.sequence; const member=selected.member;
    requireValue(member.pid !== start.pid, 'Tail cannot be the already exited root identity');
    requireValue(decimal(member.creationFileTime,true) >= decimal(start.creationFileTime,true), 'Tail identity predates the owned root');
    // Generic enumeration is diagnostic. Only pre-root, non-root/non-tail
    // observations may carry known historical uncertainty or PID reuse.
    const historicalDiagnostics={imageReadFailures:0,unavailableMembers:0,noLongerOwnedMembers:0,pidReuses:0};
    const genericIdentities=new Map();
    for (const {row,beforeRoot} of genericMembers) {
      const protectedPid=row.pid === start.pid || row.pid === member.pid;
      const historical=beforeRoot && !protectedPid;
      if (row.event !== 'owned-member' || row.image === null) {
        requireValue(historical, 'Unknown or unavailable protected/post-root generic member');
        if (row.event === 'member-unavailable') historicalDiagnostics.unavailableMembers++;
        else if (row.event === 'member-no-longer-owned') historicalDiagnostics.noLongerOwnedMembers++;
        else historicalDiagnostics.imageReadFailures++;
      }
      if (row.event !== 'owned-member') continue;
      if (protectedPid) observeIdentity(row.pid,row.creationFileTime);
      const previous=genericIdentities.get(row.pid);
      if (previous !== undefined && previous !== row.creationFileTime) {
        requireValue(historical && decimal(row.creationFileTime,true) > decimal(previous,true), 'PID reused outside historical noncandidate observations');
        historicalDiagnostics.pidReuses++;
      }
      genericIdentities.set(row.pid,row.creationFileTime);
    }
    const closed=take('cleanup.member','post-cleanup',0);
    requireValue(closed.index === 0 && closed.pid === member.pid && closed.creationFileTime === member.creationFileTime &&
      closed.waitResult === 0 && closed.identityMatches === true && closed.signaled === true, 'Held tail process did not signal with the same identity');
    for (const attempt of [1,2]) {
      const confirmation=take('cleanup.confirm','post-cleanup',attempt); const current=total(confirmation.accounting);
      requireValue(confirmation.snapshotSequence === sequence && current.activeProcesses === 0 && confirmation.fixedTotalProcesses === fixedTotal &&
        confirmation.allHeldSignaled === true && confirmation.confirmed === true, 'Missing stable empty Job/held signal confirmation');
    }
    const evidence=take('tail-evidence','post-cleanup',0);
    requireValue(cursor === stream.length && evidence.preCleanupSnapshotSequence === sequence && evidence.tailEvidenceComplete === true &&
      evidence.failure === null && evidence.diskBytesOnly === true && terminal.preCleanupSnapshotSequence === sequence, 'Tail/terminal decisive reference differs');
    return {schema:'soui.owned-tail-verdict/v1',available:true,accepted:true,failure:null,diskBytesOnly:true,
      reportSha256:actualHash,nonce:option.nonce,root:{pid:start.pid,creationFileTime:start.creationFileTime},pin:pinnedDisk,
      member:{pid:member.pid,creationFileTime:member.creationFileTime,image:member.image},fixedTotalProcesses:fixedTotal,
      preCleanupSnapshotSequence:sequence,historicalDiagnostics,terminal};
  } catch (error) { return rejected(error.message); }
}
module.exports={readTailEvidence,validateTailImagePin,parseStrictJson};
