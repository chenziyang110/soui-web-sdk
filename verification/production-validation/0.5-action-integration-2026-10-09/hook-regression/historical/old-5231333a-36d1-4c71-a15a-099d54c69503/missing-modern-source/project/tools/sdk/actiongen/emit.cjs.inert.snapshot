'use strict';

function cppString(text) {
  let result = '"';
  for (const byte of Buffer.from(text)) {
    if (byte === 34 || byte === 92) result += '\\' + String.fromCharCode(byte);
    else if (byte >= 32 && byte <= 126) result += String.fromCharCode(byte);
    else result += '\\' + byte.toString(8).padStart(3, '0');
  }
  return result + '"';
}

function emitCpp(source, note) {
  const declarations = [], registrations = [], symbols = new Set();
  function claim(name) { if (symbols.has(name)) throw new Error(`Generated C++ symbol collision: ${name}`); symbols.add(name); }
  for (const entry of source.actions) {
    let nextObject = 1;
    function type(schema, base, root = false) {
      switch (schema.type) {
        case 'string': return 'std::string';
        case 'integer': return 'std::int64_t';
        case 'number': return 'double';
        case 'boolean': return 'bool';
        case 'null': return 'std::nullptr_t';
        case 'array': return `std::vector<${type(schema.items, base)}>`;
        case 'object': {
          const name = root ? base : `${base}Object${nextObject++}`;
          claim(name);
          const required = new Set(schema.required || []);
          const fields = Object.entries(schema.properties).map(([key, schema]) => ({
            key, type: type(schema, base), optional: !required.has(key)
          }));
          declarations.push(`struct ${name} {\n${fields.map(field =>
            `  ${field.optional ? `std::optional<${field.type}>` : field.type} ${field.key}{};`).join('\n')}\n};`);
          declarations.push(`inline void from_json(const nlohmann::json& json, ${name}& value) {\n${fields.length ? '' : '  (void)json; (void)value;\n'}${fields.map(field =>
            field.optional
              ? `  if (json.contains(${cppString(field.key)})) value.${field.key} = json.at(${cppString(field.key)}).get<${field.type}>();\n  else value.${field.key}.reset();`
              : `  value.${field.key} = json.at(${cppString(field.key)}).get<${field.type}>();`).join('\n')}\n}`);
          declarations.push(`inline void to_json(nlohmann::json& json, const ${name}& value) {\n  json = nlohmann::json::object();\n${fields.length ? '' : '  (void)value;\n'}${fields.map(field =>
            field.optional ? `  if (value.${field.key}) json[${cppString(field.key)}] = *value.${field.key};`
              : `  json[${cppString(field.key)}] = value.${field.key};`).join('\n')}\n}`);
          // A data member can have the same spelling as another generated type.
          return `::${source.cppNamespace}::${name}`;
        }
        default: throw new Error('Unsupported generated type.');
      }
    }
    const input = type(entry.action.inputSchema, `${entry.cppName}Input`, true);
    const output = type(entry.action.outputSchema, `${entry.cppName}Output`, true);
    const contract = `contract_${entry.cppName}`, registration = `register_${entry.cppName}`;
    claim(contract); claim(registration);
    registrations.push(`inline constexpr const char ${contract}[] = ${cppString(JSON.stringify(entry.action))};\n` +
      `inline bool ${registration}(soui::sdk::json_bridge& bridge,\n` +
      `    std::function<${output}(const ${input}&, const soui::runtime::bridge_request&)> handler) {\n` +
      `  if (!handler) return false;\n` +
      `  return bridge.register_action(${cppString(entry.method)}, ${cppString(entry.capability)}, ${contract},\n` +
      `    [handler = std::move(handler)](const soui::runtime::bridge_request& trusted) {\n` +
      `      const auto input = nlohmann::json::parse(trusted.params_json).get<${input}>();\n` +
      `      return nlohmann::json(handler(input, trusted)).dump();\n    });\n}`);
  }
  return `// ${note}\n// Do not edit. Native authorization and schema validation remain authoritative.\n#pragma once\n` +
    '#include <soui/json_bridge.h>\n#include <json.hpp>\n#include <cstddef>\n#include <cstdint>\n#include <functional>\n#include <optional>\n#include <string>\n#include <utility>\n#include <vector>\n\n' +
    `namespace ${source.cppNamespace} {\n\n${declarations.join('\n\n')}\n\n${registrations.join('\n\n')}\n\n} // namespace ${source.cppNamespace}\n`;
}

function tsType(schema) {
  switch (schema.type) {
    case 'string': return schema.enum ? schema.enum.map(JSON.stringify).join(' | ') : 'string';
    case 'integer': case 'number': return 'number';
    case 'boolean': return 'boolean';
    case 'null': return 'null';
    case 'array': return `Array<${tsType(schema.items)}>`;
    case 'object': {
      const required = new Set(schema.required || []);
      return `{ ${Object.entries(schema.properties).map(([key, child]) =>
        `${JSON.stringify(key)}${required.has(key) ? '' : '?'}: ${tsType(child)}`).join('; ')} }`;
    }
    default: throw new Error('Unsupported generated TS type.');
  }
}
function emitTypescript(source, note) {
  const types = source.actions.map(({ cppName, action }) =>
    `export type ${cppName}Input = ${tsType(action.inputSchema)};\nexport type ${cppName}Output = ${tsType(action.outputSchema)};`).join('\n');
  const methods = source.actions.map(entry => `  ${JSON.stringify(entry.method)}: { params: ${entry.cppName}Input; result: ${entry.cppName}Output };`).join('\n');
  const contracts = source.actions.map(entry => `  [${JSON.stringify(entry.method)}]: ${JSON.stringify(entry.action)}`).join(',\n');
  return `// ${note}\n// Do not edit. These descriptors do not grant native capabilities.\n` +
    `import { validateSouiActionInput, validateSouiActionOutput, souiToolDefinitions, type SouiActionContract, type SouiCapabilityCatalog } from '@soui-sdk';\n\n` +
    `${types}\nexport interface ActionMethods {\n${methods}\n}\n` +
    `export const actionContracts: { readonly [M in keyof ActionMethods]: SouiActionContract } = {\n${contracts}\n};\n\n` +
    'export interface ActionClient {\n  call<M extends keyof ActionMethods>(method: M, input: ActionMethods[M][\'params\']): Promise<ActionMethods[M][\'result\']>;\n}\n\n' +
    'export function callAction<M extends keyof ActionMethods>(client: ActionClient, method: M, input: ActionMethods[M][\'params\']): Promise<ActionMethods[M][\'result\']> {\n' +
    '  const contract = actionContracts[method];\n  validateSouiActionInput(contract, input);\n' +
    '  // Enter the client synchronously to retain the current native input gesture.\n' +
    '  return client.call(method, input).then(result => { validateSouiActionOutput(contract, result); return result; });\n}\n\n' +
    '// Pass only a catalog obtained from this client\'s authorized native discovery.\n' +
    'export function authorizedActionTools(catalog: SouiCapabilityCatalog) {\n' +
    '  return souiToolDefinitions(catalog).filter(tool => Object.prototype.hasOwnProperty.call(actionContracts, tool.name));\n}\n';
}
function emitMarkdown(source, note) {
  const text = value => value.replace(/[<>`]/g, char => ({ '<': '&lt;', '>': '&gt;', '`': '&#96;' })[char]);
  return `<!-- ${note} -->\n# 应用动作\n\n静态描述仅供接入；调用必须使用原生已授权的会话。字符串界限按 Unicode 标量，整数限定在 JavaScript 安全范围。超时不等于取消或回滚，不自动重试。\n\n` +
    source.actions.map(({ method, capability, action }) => `## ${text(action.title)}\n\n方法：\`${method}\`；能力：\`${capability}\`。\n\n${text(action.description)}\n\n输入：\n\n\`\`\`json\n${JSON.stringify(action.inputSchema, null, 2)}\n\`\`\`\n\n成功输出：\n\n\`\`\`json\n${JSON.stringify(action.outputSchema, null, 2)}\n\`\`\`\n`).join('\n');
}
module.exports = { emitCpp, emitTypescript, emitMarkdown };
