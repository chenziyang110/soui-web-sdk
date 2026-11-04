'use strict';
// Prints only exact export marker identities, never serial body or other tags.
const fs=require('node:fs');
const serial='C:/Users/11034/.codex/task-vms/soui-devtools-qemu-stable-e89cf96f2d1e4685bbdac347cb9eea3d/private-fe2e6175cf5a4a78b23d418330506ee3/postsettings-cpu-nehalem-3f486e90/runs/166925c3-9fd3-4751-9a17-c45d880af0a6/serial.log';
const b=fs.readFileSync(serial),text=b.toString('latin1');
const rows=[...text.matchAll(/ISSUESHORT[A-F0-9]{32}BEGIN/g)].map(m=>({tag:m[0].slice(0,-5),byteOffset:m.index}));
console.log(JSON.stringify({bytesRead:b.length,exactIssueShortBeginMarkers:rows}));
