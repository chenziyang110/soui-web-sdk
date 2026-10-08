'use strict';

// Preserve these historical source/evidence files, but do not ship them as
// active dependencies. Keep old package manifests readable for rollback.
const historicalKernelBackups = new Set(['x86', 'x64'].map(arch =>
  `third_party/prebuilt/${arch}/miniblink/node.dll.before-gpu-lcd`));

function isHistoricalNativeBackup(relative) {
  return historicalKernelBackups.has(relative.toLowerCase());
}

module.exports = {isHistoricalNativeBackup};
