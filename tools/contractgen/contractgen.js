#!/usr/bin/env node
/* Schema-first generator. The generated region is deterministic and has no
 * runtime dependency beyond the standard Node.js library. */
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

function option(name, fallback) {
  const index = process.argv.indexOf(name);
  return index >= 0 && process.argv[index + 1] ? process.argv[index + 1] : fallback;
}

const input = path.resolve(option('--input', path.join(__dirname, '../../contracts/soui-native.contract.json')));
const output = path.resolve(option('--output', path.join(__dirname, '../../generated/contracts')));
const contract = JSON.parse(fs.readFileSync(input, 'utf8'));
if (contract.schema_version !== 1 || !Array.isArray(contract.methods) || contract.methods.length === 0) {
  throw new Error('contract schema_version=1 and a non-empty methods array are required');
}
const names = new Set();
for (const method of contract.methods) {
  if (!method.name || names.has(method.name)) throw new Error(`duplicate or blank method: ${method.name}`);
  names.add(method.name);
  if (!method.permission || !method.lifecycle) throw new Error(`permission/lifecycle missing: ${method.name}`);
}

function cppType(type) {
  return type === 'boolean' ? 'bool' : type === 'number' ? 'double' : 'std::string';
}
function tsType(type) {
  return type === 'boolean' ? 'boolean' : type === 'number' ? 'number' : type === 'array' ? 'unknown[]' : 'string';
}
function safeName(name) { return name.replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, ''); }
const generatedHeader = [];
generatedHeader.push('#pragma once', '#include <cstddef>', '#include <string>', '#include <vector>', '', '// <soui-generated>', 'namespace soui::generated {', `enum class ErrorCode { ${contract.errors.map(safeName).join(', ')} };`, 'struct MethodDescriptor { const char* name; const char* permission; const char* lifecycle; };', 'struct ParamDescriptor { const char* method; const char* name; const char* type; };', 'inline constexpr const char* kProtocol = "soui.bridge/1";', 'inline constexpr const char* kUnimplemented = "E_UNIMPLEMENTED";', 'inline constexpr MethodDescriptor kMethods[] = {');
for (const method of contract.methods) generatedHeader.push(`  {"${method.name}", "${method.permission}", "${method.lifecycle}"},`);
generatedHeader.push('};', `inline constexpr std::size_t kMethodCount = ${contract.methods.length};`, 'inline constexpr ParamDescriptor kParams[] = {');
let paramCount = 0;
for (const method of contract.methods) {
  for (const [name, type] of Object.entries(method.params || {})) {
    generatedHeader.push(`  {"${method.name}", "${name}", "${type}"},`);
    paramCount += 1;
  }
}
generatedHeader.push('};', `inline constexpr std::size_t kParamCount = ${paramCount};`, '} // namespace soui::generated', '// </soui-generated>', '');

const generatedTs = [];
generatedTs.push('/* <soui-generated> */', `export const SOUI_BRIDGE_PROTOCOL = '${contract.protocol}';`, 'export type SouiErrorCode = ' + contract.errors.map(e => `'${e}'`).join(' | ') + ';', 'export interface SouiMethodMap {');
for (const method of contract.methods) {
  generatedTs.push(`  '${method.name}': { params: {`);
  for (const [key, type] of Object.entries(method.params || {})) generatedTs.push(`    ${key}: ${tsType(type)};`);
  generatedTs.push('  }; result: {');
  for (const [key, type] of Object.entries(method.result || {})) generatedTs.push(`    ${key}: ${tsType(type)};`);
  generatedTs.push('  }; };');
}
generatedTs.push('}', 'export type SouiMethodName = keyof SouiMethodMap;', 'export interface SouiNativeClient {', '  call<M extends SouiMethodName>(method: M, params: SouiMethodMap[M]["params"]): Promise<SouiMethodMap[M]["result"]>;', '  query(taskId: string): Promise<SouiMethodMap["task.query"]["result"]>;', '  cancel(taskId: string): Promise<SouiMethodMap["task.cancel"]["result"]>;', '  subscribe(taskId: string, eventName: string, listener: (event: unknown) => void): () => void;', '}', '/* </soui-generated> */', '');

fs.mkdirSync(output, { recursive: true });
const header = generatedHeader.join('\n');
const types = generatedTs.join('\n');
fs.writeFileSync(path.join(output, 'soui_native.generated.h'), header);
fs.writeFileSync(path.join(output, 'soui_native.generated.ts'), types);
const digest = value => crypto.createHash('sha256').update(value).digest('hex');
const sourceBytes = fs.readFileSync(input);
const manifest = {
  schema_version: 1,
  protocol: contract.protocol,
  source: path.relative(process.cwd(), input).replace(/\\/g, '/'),
  source_sha256: digest(sourceBytes),
  outputs: {
    cpp: { path: path.relative(process.cwd(), path.join(output, 'soui_native.generated.h')).replace(/\\/g, '/'), sha256: digest(header) },
    typescript: { path: path.relative(process.cwd(), path.join(output, 'soui_native.generated.ts')).replace(/\\/g, '/'), sha256: digest(types) }
  },
  method_count: contract.methods.length,
  errors: contract.errors,
  status: 'generated'
};
fs.writeFileSync(path.join(output, 'generated-manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
console.log(JSON.stringify(manifest, null, 2));
