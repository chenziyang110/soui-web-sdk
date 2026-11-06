'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const [root, output] = process.argv.slice(2);
if (!root || !output || process.argv.length !== 4)
  throw new Error('usage: generate.cjs source-root output');
const source = 'sdk/src/host/soui_miniblink_host.cpp';
const bytes = fs.readFileSync(path.join(root, source));
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
const entries = [];
function extract(begin, end) {
  const first = Buffer.from(begin, 'ascii'), last = Buffer.from(end, 'ascii');
  const start = bytes.indexOf(first);
  const stop = bytes.indexOf(last, start + first.length);
  if (start < 0 || stop <= start || bytes.indexOf(first, start + first.length) >= 0)
    throw new Error('missing or ambiguous original method: ' + begin);
  const fragment = bytes.subarray(start, stop);
  if (fragment.some(value => value > 127)) throw new Error('expected exact ASCII method bytes');
  entries.push({begin, end, startByte: start, endByteExclusive: stop, sha256: sha(fragment)});
  return fragment;
}
const fragments = [
  extract('constexpr std::size_t max_bridge_message_bytes = 64U * 1024U;', 'std::string packet_string('),
  extract('std::string packet_string(', 'std::string bridge_error('),
  extract('std::string bridge_request_method(', '// The pinned engine owns process-wide Blink/V8 state')
];
fs.mkdirSync(output, {recursive: true});
function writeIfChanged(name, value) {
  const target = path.join(output, name), buffer = Buffer.isBuffer(value) ? value : Buffer.from(value);
  if (!fs.existsSync(target) || !fs.readFileSync(target).equals(buffer)) fs.writeFileSync(target, buffer);
}
const projected = Buffer.concat(fragments);
writeIfChanged('original-methods.inc', projected);
writeIfChanged('source-binding.json', JSON.stringify({
  schema: 'soui.miniblink-request-route-source/v1', source, sourceSha256: sha(bytes),
  generatedSha256: sha(projected), entries,
  scope: 'Exact bounded packet and method extraction only; no Host execution or lifecycle projection.'
}, null, 2) + '\n');
