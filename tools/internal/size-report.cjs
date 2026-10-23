'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {safePath} = require('./delivery-identity.cjs');

const categoryNames = ['images', 'javascript', 'css', 'fonts', 'html', 'legal', 'native', 'sourceMaps', 'media', 'data', 'other'];
const comparePath = (a, b) => a < b ? -1 : a > b ? 1 : 0;
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const integer = value => Number.isSafeInteger(value) && value >= 0;
function add(a, b) {
  const sum = a + b;
  if (!Number.isSafeInteger(sum)) throw new Error('Size total exceeds safe integer range');
  return sum;
}

function category(name) {
  if (/(^|\/)(licenses?|legal)(\/|\.)/i.test(name) || /(^|\/)(licen[cs]e|copying|notice|third_party_notices)([-_.]|$)/i.test(name)) return 'legal';
  const extension = path.posix.extname(name).toLowerCase();
  if (['.png', '.jpg', '.jpeg', '.gif', '.svg', '.webp', '.ico', '.bmp', '.avif'].includes(extension)) return 'images';
  if (['.js', '.mjs', '.cjs'].includes(extension)) return 'javascript';
  if (extension === '.css') return 'css';
  if (['.woff', '.woff2', '.ttf', '.otf', '.eot'].includes(extension)) return 'fonts';
  if (['.html', '.htm'].includes(extension)) return 'html';
  if (['.exe', '.dll', '.lib', '.pdb', '.sys', '.ocx'].includes(extension)) return 'native';
  if (extension === '.map') return 'sourceMaps';
  if (['.mp3', '.mp4', '.webm', '.ogg', '.wav', '.flac', '.avi'].includes(extension)) return 'media';
  if (['.json', '.xml', '.txt', '.bin', '.dat', '.wasm'].includes(extension)) return 'data';
  return 'other';
}

function identity(report) {
  const result = {};
  for (const [key, alias] of [['version'], ['target'], ['architecture', 'arch'], ['configuration', 'config'], ['rendererBackend', 'backend'], ['webTarget']]) {
    const value = report[key] === undefined ? alias && report[alias] : report[key];
    if (alias && report[key] !== undefined && report[alias] !== undefined && report[key] !== report[alias]) throw new Error(`Conflicting size identity: ${key}`);
    if (value !== undefined && value !== null && (typeof value !== 'string' || !value.trim() || value.length > 256)) throw new Error(`Invalid size identity: ${key}`);
    result[key] = value === undefined ? null : value;
  }
  return result;
}

function inventory(report) {
  if (!object(report)) throw new Error('Size report must be an object');
  const payload = report.payload === undefined ? report : report.payload;
  if (!object(payload) || !Array.isArray(payload.files)) throw new Error('Size report requires a payload file manifest');
  const seen = new Set();
  let bytes = 0;
  const files = payload.files.map(file => {
    if (!object(file) || typeof file.path !== 'string' || !file.path || !integer(file.bytes) ||
        typeof file.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(file.sha256)) throw new Error('Invalid size manifest file');
    safePath(process.cwd(), file.path);
    const key = file.path.toLowerCase();
    if (seen.has(key)) throw new Error(`Duplicate size manifest path: ${file.path}`);
    seen.add(key);
    bytes = add(bytes, file.bytes);
    return {path: file.path, bytes: file.bytes, sha256: file.sha256};
  }).sort((a, b) => comparePath(a.path, b.path));
  if (payload.bytes !== undefined && (!integer(payload.bytes) || payload.bytes !== bytes)) throw new Error('Size manifest byte total does not match its files');
  return {identity: identity(report), files, bytes};
}

function difference(current, previous) {
  for (const key of ['target', 'architecture', 'configuration', 'rendererBackend', 'webTarget']) {
    if ((key !== 'webTarget' && (!current.identity[key] || !previous.identity[key])) || current.identity[key] !== previous.identity[key]) {
      throw new Error(`Incomparable size baseline: ${key}`);
    }
  }
  const before = new Map(previous.files.map(file => [file.path.toLowerCase(), file]));
  const after = new Map(current.files.map(file => [file.path.toLowerCase(), file]));
  const added = [], removed = [], changed = [];
  for (const file of current.files) {
    const old = before.get(file.path.toLowerCase());
    if (!old) added.push(file);
    else if (old.bytes !== file.bytes || old.sha256 !== file.sha256 || old.path !== file.path) changed.push({
      path: file.path, previousPath: old.path, beforeBytes: old.bytes, bytes: file.bytes,
      deltaBytes: file.bytes - old.bytes, beforeSha256: old.sha256, sha256: file.sha256
    });
  }
  for (const file of previous.files) if (!after.has(file.path.toLowerCase())) removed.push(file);
  return {baseline: previous.identity, baselineBytes: previous.bytes, deltaBytes: current.bytes - previous.bytes, added, removed, changed};
}

function checkBudget(budget, bytes, comparison) {
  if (!object(budget) || Object.keys(budget).some(key => !['totalBytes', 'growthBytes'].includes(key)) ||
      Object.values(budget).some(value => !integer(value))) throw new Error('Size budget permits only nonnegative safe integer totalBytes and growthBytes');
  if (budget.growthBytes !== undefined && !comparison) throw new Error('Size growth budget requires a comparable baseline');
  const violations = [];
  if (budget.totalBytes !== undefined && bytes > budget.totalBytes) violations.push({metric: 'totalBytes', actual: bytes, limit: budget.totalBytes});
  if (budget.growthBytes !== undefined && comparison.deltaBytes > budget.growthBytes) violations.push({metric: 'growthBytes', actual: comparison.deltaBytes, limit: budget.growthBytes});
  return {limits: {
    ...(budget.totalBytes === undefined ? {} : {totalBytes: budget.totalBytes}),
    ...(budget.growthBytes === undefined ? {} : {growthBytes: budget.growthBytes})
  }, passed: violations.length === 0, violations};
}

// Reports describe the supplied manifest. Duplicate potential is uncompressed bytes,
// not an estimate of archive savings or proof that different runtimes are compatible.
function summarize(report, options = {}) {
  if (!object(options)) throw new Error('Size analysis options must be an object');
  const topCount = options.topCount === undefined ? 20 : options.topCount;
  if (!integer(topCount) || topCount < 1 || topCount > 1000) throw new Error('topCount must be between 1 and 1000');
  const current = inventory(report);
  const categories = Object.fromEntries(categoryNames.map(name => [name, {bytes: 0, fileCount: 0}]));
  const groups = new Map();
  for (const file of current.files) {
    const group = categories[category(file.path)];
    group.bytes = add(group.bytes, file.bytes);
    group.fileCount++;
    const key = `${file.sha256}:${file.bytes}`;
    if (!groups.has(key)) groups.set(key, {sha256: file.sha256, bytes: file.bytes, paths: []});
    groups.get(key).paths.push(file.path);
  }
  const duplicates = [...groups.values()].filter(group => group.paths.length > 1).map(group => ({
    ...group, potentialUncompressedBytes: group.bytes * (group.paths.length - 1)
  })).sort((a, b) => b.potentialUncompressedBytes - a.potentialUncompressedBytes || comparePath(a.paths[0], b.paths[0]));
  const comparison = options.baseline === undefined ? null : difference(current, inventory(options.baseline));
  return {schema: 'soui.size-analysis/v1', schemaVersion: 1, identity: current.identity, bytes: current.bytes,
    fileCount: current.files.length, categories,
    topFiles: [...current.files].sort((a, b) => b.bytes - a.bytes || comparePath(a.path, b.path)).slice(0, topCount),
    duplicates, potentialDuplicateBytes: duplicates.reduce((sum, entry) => add(sum, entry.potentialUncompressedBytes), 0),
    comparison, budget: options.budget === undefined ? null : checkBudget(options.budget, current.bytes, comparison)};
}

// This is an input parsing bound, unrelated to any application's size budget.
const maxJsonBytes = 64 * 1024 * 1024;
function readJson(file) {
  const fd = fs.openSync(file, 'r');
  try {
    const stat = fs.fstatSync(fd);
    if (!stat.isFile() || stat.size > maxJsonBytes) throw new Error('Size report JSON exceeds the input parsing limit or is not a file');
    const data = Buffer.alloc(stat.size + 1);
    let count = 0, read;
    while (count < data.length && (read = fs.readSync(fd, data, count, data.length - count, null)) > 0) count += read;
    if (count > stat.size) throw new Error('Size report JSON changed during read');
    return JSON.parse(data.subarray(0, count).toString('utf8').replace(/^\uFEFF/, ''));
  } finally { fs.closeSync(fd); }
}

function cli(args) {
  const inputs = {};
  while (args.length) {
    const flag = args.shift();
    if (!['--report', '--baseline', '--budget'].includes(flag) || inputs[flag] || !args.length || args[0].startsWith('--')) throw new Error('Usage: size-report.cjs --report file [--baseline file] [--budget file]');
    inputs[flag] = args.shift();
  }
  if (!inputs['--report']) throw new Error('Size report input is required');
  const result = summarize(readJson(inputs['--report']), {
    ...(inputs['--baseline'] ? {baseline: readJson(inputs['--baseline'])} : {}),
    ...(inputs['--budget'] ? {budget: readJson(inputs['--budget'])} : {})
  });
  console.log(JSON.stringify(result, null, 2));
  if (result.budget && !result.budget.passed) process.exitCode = 1;
}

module.exports = {summarize};
if (require.main === module) {
  try { cli(process.argv.slice(2)); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
