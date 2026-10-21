#!/usr/bin/env node
'use strict';

// Local source generator only: no downloads, execution, SDK discovery or installs.
const fs = require('node:fs');
const path = require('node:path');
const { TextDecoder } = require('node:util');

function portableName(name) {
  return name.length > 0 && Buffer.byteLength(name) <= 240 && name === name.normalize('NFC') &&
    !/[<>:"/\\|?*\x00-\x1f\x7f-\x9f]/.test(name) && !/[. ]$/.test(name) &&
    name !== '.' && name !== '..' && !/^(con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³])(?:\.|$)/i.test(name);
}
function validate(options) {
  if (!options || Object.keys(options).some(key => !['output', 'name', 'identifier', 'version'].includes(key)))
    throw new Error('Unknown project option.');
  if (typeof options.output !== 'string' || !options.output || options.output.includes('\0'))
    throw new Error('A new output directory is required.');
  if (typeof options.name !== 'string' || !/^[A-Za-z][A-Za-z0-9_]{0,63}$/.test(options.name) || !portableName(options.name))
    throw new Error('Name must start with an ASCII letter and contain at most 64 letters, digits or underscores.');
  if (new Set(['all', 'all_build', 'clean', 'help', 'install', 'test', 'run_tests', 'package',
      'package_source', 'edit_cache', 'rebuild_cache', 'preinstall', 'list_install_components',
      'zero_check', 'web']).has(options.name.toLowerCase()))
    throw new Error('Application name is reserved by the build or staging layout.');
  if (typeof options.identifier !== 'string' || options.identifier.length > 240 ||
      !/^[A-Za-z][A-Za-z0-9-]*(?:\.[A-Za-z][A-Za-z0-9-]*)+$/.test(options.identifier))
    throw new Error('An explicit reverse-DNS identifier is required.');
  if (typeof options.version !== 'string' || !/^\d{1,5}\.\d{1,5}\.\d{1,5}$/.test(options.version))
    throw new Error('Application version must contain three numeric components.');
  return options;
}
function parseArgs(args) {
  const options = { version: '0.1.0' }, seen = new Set();
  for (let index = 0; index < args.length; index += 2) {
    const key = args[index].slice(2);
    if (!args[index].startsWith('--') || !['output', 'name', 'identifier', 'version'].includes(key) ||
        seen.has(key) || !args[index + 1] || args[index + 1].startsWith('--'))
      throw new Error('Usage: create-desktop-app.cjs --output NEW_DIR --name AppName --identifier org.example.app [--version 0.1.0]');
    seen.add(key); options[key] = args[index + 1];
  }
  return validate(options);
}
function noSymlinks(input) {
  const absolute = path.resolve(input), root = path.parse(absolute).root;
  let current = root;
  for (const part of absolute.slice(root.length).split(path.sep).filter(Boolean)) {
    current = path.join(current, part);
    if (fs.lstatSync(current).isSymbolicLink()) throw new Error('Symlink paths are not allowed.');
  }
  return absolute;
}
function within(parent, child) {
  const relative = path.relative(parent, child);
  return !relative || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
}
function templateDirectory() {
  // Source tools/../templates and installed share/SouiDesktop/tools/../templates.
  return noSymlinks(path.join(__dirname, '..', 'templates', 'unified-desktop'));
}
function collectTemplates(root, replacements, required) {
  const files = [];
  let entries = 0, total = 0;
  const decoder = new TextDecoder('utf-8', { fatal: true });
  function walk(directory, relative, depth) {
    if (depth > 8) throw new Error('Template nesting limit exceeded.');
    const names = new Set();
    for (const name of fs.readdirSync(directory).sort()) {
      if (++entries > 64 || !portableName(name)) throw new Error('Invalid or excessive template entries.');
      const folded = name.toLowerCase();
      if (names.has(folded)) throw new Error('Case-colliding template entries.');
      names.add(folded);
      const source = path.join(directory, name), destination = path.join(relative, name);
      const stat = fs.lstatSync(source);
      if (stat.isSymbolicLink() || (!stat.isDirectory() && !stat.isFile())) throw new Error('Template must contain only ordinary files and directories.');
      if (stat.isDirectory()) { walk(source, destination, depth + 1); continue; }
      total += stat.size;
      if (stat.size > 64 * 1024 || total > 512 * 1024) throw new Error('Template size limit exceeded.');
      const descriptor = fs.openSync(source, 'r');
      const buffer = Buffer.alloc(64 * 1024 + 1);
      let count;
      try { count = fs.readSync(descriptor, buffer, 0, buffer.length, 0); }
      finally { fs.closeSync(descriptor); }
      if (count !== stat.size) throw new Error('Template changed during generation.');
      let content = decoder.decode(buffer.subarray(0, count));
      content = content.replace(/@APP_[A-Z_]+@/g, token => {
        if (!Object.hasOwn(replacements, token)) throw new Error('Unknown template token.');
        return replacements[token];
      });
      files.push({ destination, content });
    }
  }
  walk(root, '', 0);
  for (const name of required)
    if (!files.some(file => file.destination === name)) throw new Error(`Incomplete project template: missing ${name}.`);
  return files;
}
function createProject(input) {
  const options = validate(input);
  const root = templateDirectory(), output = path.resolve(options.output);
  const parent = noSymlinks(path.dirname(output));
  if (!fs.statSync(parent).isDirectory() || !portableName(path.basename(output))) throw new Error('Invalid destination directory.');
  if (fs.existsSync(output)) throw new Error('Output already exists; refusing to overwrite.');
  if (within(root, output) || within(output, root)) throw new Error('Output and template trees must not overlap.');
  const files = collectTemplates(root, {
    '@APP_NAME@': options.name, '@APP_IDENTIFIER@': options.identifier, '@APP_VERSION@': options.version,
  }, [
    'CMakeLists.txt', 'main.cpp',
    // Include the full starter module graph and its fetch/style dependencies.
    // A partial SDK installation must fail here, before creating any output.
    ...['index.html', 'app.mjs', 'feature.mjs', 'details.mjs', 'config.json', 'style.css']
      .map(name => path.join('web', name)),
  ]);
  // Copy the public adapter from its sole SDK source, never a divergent template fork.
  const installedFrontend = path.join(__dirname, '..', 'frontend');
  const frontend = noSymlinks(fs.existsSync(installedFrontend) ? installedFrontend :
    path.join(__dirname, '..', 'sdk', 'desktop', 'frontend'));
  if (within(frontend, output) || within(output, frontend))
    throw new Error('Output and frontend adapter trees must not overlap.');
  const adapterFiles = collectTemplates(frontend, {}, ['desktop.mjs', 'desktop.d.mts']);
  if (adapterFiles.length !== 2) throw new Error('Unexpected frontend adapter entries.');
  const adapterPrefix = path.join('web', 'soui').toLowerCase();
  if (files.some(existing => existing.destination.toLowerCase() === adapterPrefix ||
      existing.destination.toLowerCase().startsWith(adapterPrefix + path.sep)))
    throw new Error('Frontend adapter destination collides with template.');
  for (const file of adapterFiles) {
    const destination = path.join('web', 'soui', file.destination);
    files.push({ destination, content: file.content });
  }
  fs.mkdirSync(output); // Atomic no-overwrite. Failure never triggers recursive cleanup.
  for (const file of files) {
    const destination = path.join(output, file.destination);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    fs.writeFileSync(destination, file.content, { flag: 'wx', mode: 0o644 });
  }
  return { name: options.name, files: files.length };
}
if (require.main === module) {
  try {
    const result = createProject(parseArgs(process.argv.slice(2)));
    console.log(`Created ${result.name}: ${result.files} files. Configure with an installed SouiDesktop SDK; no build or download was performed.`);
  } catch (error) {
    console.error(`Project generation failed: ${error.message} Partial output, if any, is retained.`);
    process.exitCode = 1;
  }
}
module.exports = { parseArgs, createProject };
