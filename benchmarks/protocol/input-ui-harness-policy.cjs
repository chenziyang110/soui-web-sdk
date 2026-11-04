'use strict';
const {insist}=require('./input-frame-trace.cjs');
const fields=['suppress_focus','borderless','foreground','logging'];
function requireFormalHarness(record) {
  insist(record?.measurement_path==='public-sdk'&&record.diagnostics&&
    Object.keys(record.diagnostics).length===fields.length&&
    fields.every(name=>record.diagnostics[name]===false),
    'Formal sampling requires an explicitly identified public SDK path with every diagnostic intervention disabled');
}
module.exports={requireFormalHarness};
