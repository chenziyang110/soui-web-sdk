/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @constructor
 */
function SourceMapV3()
{
    /** @type {number} */ this.version;
    /** @type {string|undefined} */ this.file;
    /** @type {!Array.<string>} */ this.sources;
    /** @type {!Array.<!SourceMapV3.Section>|undefined} */ this.sections;
    /** @type {string} */ this.mappings;
    /** @type {string|undefined} */ this.sourceRoot;
}

/**
 * @constructor
 */
SourceMapV3.Section = function()
{
    /** @type {!SourceMapV3} */ this.map;
    /** @type {!SourceMapV3.Offset} */ this.offset;
}

/**
 * @constructor
 */
SourceMapV3.Offset = function()
{
    /** @type {number} */ this.line;
    /** @type {number} */ this.column;
}

/**
 * Implements Source Map V3 model. See http://code.google.com/p/closure-compiler/wiki/SourceMaps
 * for format description.
 * @constructor
 * @param {string} sourceMappingURL
 * @param {!SourceMapV3} payload
 */
WebInspector.SourceMap = function(sourceMappingURL, payload)
{
    if (!WebInspector.SourceMap.prototype._base64Map) {
        const base64Digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        WebInspector.SourceMap.prototype._base64Map = {};
        for (var i = 0; i < base64Digits.length; ++i)
            WebInspector.SourceMap.prototype._base64Map[base64Digits.charAt(i)] = i;
    }

    this._sourceMappingURL = sourceMappingURL;
    this._reverseMappingsBySourceURL = Object.create(null);
    this._mappings = [];
    this._sources = Object.create(null);
    this._sourceContentByURL = Object.create(null);
    this._parseMappingPayload(payload);
}

/**
 * @param {string} sourceMapURL
 * @param {string} compiledURL
 * @param {function(?WebInspector.SourceMap)} callback
 * @this {WebInspector.SourceMap}
 */
// Private controlled-inspector data interpreter. No URL loading or code evaluation.
var Stage0InlineMaps = (function() {
    "use strict";
    // ADR 0130: legacy maps need 77,525 segments and 286 sources. Model and
    // binding capacity share the unchanged 16 MiB target logical-byte budget.
    var limits = {"urlBytes":1048576,"decodedBytes":1048576,"cacheRecordsIncludingActiveRetired":8,"targetLogicalBytes":16777216,"jsonDepth":2,"arrayElements":20000,"sources":512,"names":8192,"labelChars":2048,"nameChars":4096,"segments":131072,"generatedLines":65536,"sourceLinesTotal":65536,"column":1048576,"vlqDigits":6,"vlqUnsigned":2147483647,"snapshotFiles":1024,"snapshotFileBytes":1048576,"snapshotTotalBytes":16777216,"bindingLedgerEntries":512,"bindingLedgerEntryLogicalBytes":256,"bindingBorrowMaximum":8};
    var owners = typeof WeakMap === "function" ? new WeakMap() : null;
    var models = owners ? new WeakMap() : null;
    var leases = owners ? new WeakMap() : null;
    var diagnosticErrorCodes = {"E_INLINE_STATE_API":1,"E_INLINE_BUDGET":2,"E_INLINE_URL":3,"E_INLINE_HEADER":4,"E_INLINE_BASE64":5,"E_INLINE_BYTES":6,"E_INLINE_UTF8_API":7,"E_INLINE_BOM":8,"E_INLINE_UTF8":9,"E_INLINE_JSON":10,"E_INLINE_NODES":11,"E_INLINE_KEY":12,"E_INLINE_DUPLICATE_KEY":13,"E_INLINE_DEPTH":14,"E_INLINE_LABEL":15,"E_INLINE_MAP":16,"E_INLINE_FIELDS":17,"E_INLINE_SOURCE_URL":18,"E_INLINE_CONTENT":19,"E_INLINE_SOURCE_LINES":20,"E_INLINE_NAMES":21,"E_INLINE_IGNORE":22,"E_INLINE_VLQ_FIELDS":23,"E_INLINE_GENERATED_LINES":24,"E_INLINE_VLQ":25,"E_INLINE_VLQ_OVERFLOW":26,"E_INLINE_SEGMENTS":27,"E_INLINE_COLUMN":28,"E_INLINE_COORDINATE":29,"E_INLINE_NAME_INDEX":30,"E_INLINE_STATE":31,"E_INLINE_BASE":32,"E_INLINE_PENDING":33,"E_INLINE_SLOTS":34,"E_INLINE_STALE":35,"E_INLINE_BINDING_MODEL":36,"E_INLINE_BINDING_SLOTS":37,"E_INLINE_BINDING_BUDGET":38,"E_INLINE_PARSE":39};
    var verifiedDecoder = null;
    var alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    var own = function(o, k) { return Object.prototype.hasOwnProperty.call(o, k); };
    var errorCodes = Object.create(null);
    ("STATE_API BUDGET URL HEADER BASE64 BYTES UTF8_API BOM UTF8 JSON NODES KEY DUPLICATE_KEY DEPTH LABEL MAP FIELDS SOURCE_URL CONTENT SOURCE_LINES NAMES IGNORE VLQ_FIELDS GENERATED_LINES VLQ VLQ_OVERFLOW SEGMENTS COLUMN COORDINATE NAME_INDEX STATE BASE PENDING SLOTS STALE BINDING_MODEL BINDING_SLOTS BINDING_BUDGET").split(" ").forEach(function(code) {
        errorCodes["E_INLINE_" + code] = true;
    });
    var integer = function(n) { return typeof n === "number" && isFinite(n) && Math.floor(n) === n; };
    function fail(code) { throw new Error(code); }
    function need(value, code) { if (!value) fail(code); }
    function stateOf(handle) { return owners && handle && typeof handle === "object" ? owners.get(handle) : null; }
    function current(s, epoch) { return s && !s.disposed && !s.resetDepth && s.epoch === epoch; }
    function retire(s, record) {
        if (record.released || record.cached || record.active || record.bindings) return;
        record.released = true;
        if (s.cache[record.key] === record) delete s.cache[record.key];
        s.bytes -= record.charge;
        var index = s.records.indexOf(record);
        if (index !== -1) s.records.splice(index, 1);
        record.model = null;
    }
    function clear(s) {
        if (s.epoch >= 2147483647) s.disposed = true;
        else ++s.epoch;
        s.cache = Object.create(null);
        var records = s.records.slice();
        for (var i = 0; i < records.length; ++i) {
            records[i].cached = false;
            retire(s, records[i]);
        }
    }
    function create() {
        need(owners, "E_INLINE_STATE_API");
        var handle = Object.freeze({});
        owners.set(handle, {epoch: 1, disposed: false, resetDepth: 0, bytes: 0,
            records: [], cache: Object.create(null), lastError: "", bindingCount: 0, bindingBytes: 0,
            diagnosticDepth: 0, diagnosticWord: -1, diagnosticUnknown: false});
        return handle;
    }
    function beginReset(handle) {
        var s = stateOf(handle); if (!s) return;
        ++s.resetDepth; clear(s);
    }
    function endReset(handle) {
        var s = stateOf(handle); if (s && s.resetDepth) --s.resetDepth;
    }
    function dispose(handle) {
        var s = stateOf(handle); if (!s) return;
        s.disposed = true; clear(s);
    }
    function status(handle) {
        var s = stateOf(handle);
        return Object.freeze(s ? {epoch: s.epoch, disposed: s.disposed, resetting: !!s.resetDepth,
            logicalBytes: s.bytes, records: s.records.length, lastError: s.lastError,
            bindingRecords: s.bindingCount, bindingLogicalBytes: s.bindingBytes}
            : {epoch: -1, disposed: true, resetting: true, logicalBytes: 0, records: 0, lastError: "E_INLINE_STATE"});
    }
    function reserve(s, record, charge) {
        need(integer(charge) && charge >= record.charge && charge <= limits.targetLogicalBytes
            && s.bytes + charge - record.charge <= limits.targetLogicalBytes, "E_INLINE_BUDGET");
        s.bytes += charge - record.charge; record.charge = charge;
    }
    function retireLease(lease) {
        if (lease.released || lease.owner || lease.uses) return;
        lease.released = true;
        --lease.s.bindingCount;
        lease.s.bindingBytes -= limits.bindingLedgerEntryLogicalBytes;
        lease.s.bytes -= limits.bindingLedgerEntryLogicalBytes;
        --lease.record.bindings;
        retire(lease.s, lease.record);
    }
    function reserveBinding(handle, epoch, model) {
        var s = stateOf(handle), held = models && models.get(model);
        need(current(s, epoch) && held && held.s === s && held.epoch === epoch
            && !held.record.released && held.record.model === model, "E_INLINE_BINDING_MODEL");
        need(s.bindingCount < limits.bindingLedgerEntries, "E_INLINE_BINDING_SLOTS");
        need(s.bytes + limits.bindingLedgerEntryLogicalBytes <= limits.targetLogicalBytes, "E_INLINE_BINDING_BUDGET");
        ++s.bindingCount; s.bindingBytes += limits.bindingLedgerEntryLogicalBytes;
        s.bytes += limits.bindingLedgerEntryLogicalBytes; ++held.record.bindings;
        var token = Object.freeze({});
        leases.set(token, {s: s, record: held.record, epoch: epoch, owner: true, uses: 1, released: false});
        return token;
    }
    function beginBinding(handle, token) {
        var s = stateOf(handle), lease = leases && leases.get(token);
        if (!lease || lease.s !== s || lease.released || !lease.owner || !current(s, lease.epoch)
            || lease.uses >= limits.bindingBorrowMaximum) return false;
        ++lease.uses; return true;
    }
    function endBinding(token) {
        var lease = leases && leases.get(token);
        if (!lease || lease.released || !lease.uses) return;
        --lease.uses; retireLease(lease);
    }
    function releaseBinding(token) {
        var lease = leases && leases.get(token);
        if (!lease || lease.released) return;
        lease.owner = false; retireLease(lease);
    }
    function header(url) {
        need(typeof url === "string" && url.length <= limits.urlBytes, "E_INLINE_URL");
        var a = "data:application/json;base64,", b = "data:application/json;charset=utf-8;base64,";
        var start = url.indexOf(a) === 0 ? a.length : url.indexOf(b) === 0 ? b.length : -1;
        need(start >= 0, "E_INLINE_HEADER");
        var text = url.substring(start);
        need(text.length && text.length % 4 === 0 && /^[A-Za-z0-9+/]*={0,2}$/.test(text), "E_INLINE_BASE64");
        var padding = text.charAt(text.length - 1) === "=" ? (text.charAt(text.length - 2) === "=" ? 2 : 1) : 0;
        var bytes = text.length / 4 * 3 - padding;
        need(bytes > 0 && bytes <= limits.decodedBytes, "E_INLINE_BYTES");
        return {text: text, bytes: bytes};
    }
    // Private fallback: at most decodedBytes input/UTF16 units, 1024-unit chunks,
    // at most ceil(decodedBytes/1024) retained chunks; no apply/spread call stack.
    function decodeUTF8Strict(bytes) {
        need(bytes.length <= limits.decodedBytes, "E_INLINE_BYTES");
        var chunks = [], chunk = "", units = 0, i = 0;
        while (i < bytes.length) {
            var first = bytes[i++], value, extra, minimum;
            if (first <= 127) { value = first; extra = 0; minimum = 0; }
            else if (first >= 194 && first <= 223) { value = first & 31; extra = 1; minimum = 128; }
            else if (first >= 224 && first <= 239) { value = first & 15; extra = 2; minimum = 2048; }
            else if (first >= 240 && first <= 244) { value = first & 7; extra = 3; minimum = 65536; }
            else fail("E_INLINE_UTF8");
            need(i + extra <= bytes.length, "E_INLINE_UTF8");
            for (var n = 0; n < extra; ++n) {
                var next = bytes[i++]; need(next >= 128 && next <= 191, "E_INLINE_UTF8");
                value = value * 64 + (next & 63);
            }
            need(value >= minimum && value <= 1114111 && !(value >= 55296 && value <= 57343), "E_INLINE_UTF8");
            var text;
            if (value < 65536) text = String.fromCharCode(value);
            else { value -= 65536; text = String.fromCharCode(55296 + Math.floor(value / 1024), 56320 + value % 1024); }
            units += text.length; need(units <= bytes.length, "E_INLINE_BYTES");
            if (chunk.length + text.length > 1024) { chunks.push(chunk); chunk = ""; }
            chunk += text;
        }
        if (chunk.length) chunks.push(chunk);
        need(chunks.length <= Math.ceil(limits.decodedBytes / 1024), "E_INLINE_BYTES");
        return chunks.join("");
    }
    function utf8(encoded, diagnostic) {
        need(typeof atob === "function" && (diagnostic.mask |= 1)
            && typeof btoa === "function" && (diagnostic.mask |= 2)
            && typeof Uint8Array === "function" && (diagnostic.mask |= 4), "E_INLINE_UTF8_API");
        var hasNativeDecoder = typeof TextDecoder === "function";
        if (hasNativeDecoder) diagnostic.mask |= 8;
        if (hasNativeDecoder && verifiedDecoder !== TextDecoder) {
            var probe = new TextDecoder("utf-8", {fatal: true}), rejected = false;
            try { probe.decode(new Uint8Array([192])); } catch (probeError) { rejected = true; }
            need(rejected && new TextDecoder("utf-8", {fatal: true}).decode(new Uint8Array([226, 130, 172])) === "\u20ac", "E_INLINE_UTF8_API");
            verifiedDecoder = TextDecoder;
        }
        var binary = atob(encoded.text);
        need(binary.length === encoded.bytes && btoa(binary) === encoded.text, "E_INLINE_BASE64");
        need(!(binary.length >= 3 && binary.charCodeAt(0) === 239 && binary.charCodeAt(1) === 187
            && binary.charCodeAt(2) === 191), "E_INLINE_BOM");
        var bytes = new Uint8Array(binary.length);
        for (var i = 0; i < binary.length; ++i) bytes[i] = binary.charCodeAt(i);
        if (!hasNativeDecoder) return decodeUTF8Strict(bytes);
        var decoder = new TextDecoder("utf-8", {fatal: true});
        need(decoder.fatal === true && decoder.encoding === "utf-8", "E_INLINE_UTF8_API");
        try { return decoder.decode(bytes); } catch (e) { fail("E_INLINE_UTF8"); }
    }
    // Only a flat object with scalar properties and flat arrays is supported.
    // Key strings are parsed separately so escaped duplicate keys cannot hide.
    function preflightJSON(text) {
        var depth = 0, expectKey = false, arrayValue = false, count = 0;
        var seen = Object.create(null), rootSeen = false;
        for (var i = 0; i < text.length; ++i) {
            var c = text.charAt(i);
            if (/\s/.test(c)) continue;
            if (depth === 2 && arrayValue && c !== "]") { need(++count <= limits.arrayElements, "E_INLINE_NODES"); arrayValue = false; }
            if (c === '"') {
                var start = i, closed = false;
                while (++i < text.length) {
                    var v = text.charAt(i);
                    if (v === "\\") { ++i; continue; }
                    if (v === '"') { closed = true; break; }
                }
                need(closed, "E_INLINE_JSON");
                if (depth === 1 && expectKey) {
                    need(i - start <= 64, "E_INLINE_KEY");
                    var key;
                    try { key = JSON.parse(text.substring(start, i + 1)); } catch (e) { fail("E_INLINE_JSON"); }
                    need(!own(seen, key), "E_INLINE_DUPLICATE_KEY"); seen[key] = true;
                    expectKey = false;
                    var colon = i + 1;
                    while (/\s/.test(text.charAt(colon)) && colon < text.length) ++colon;
                    need(text.charAt(colon) === ":", "E_INLINE_JSON"); i = colon;
                }
                continue;
            }
            if (c === "{") { need(!rootSeen && depth === 0, "E_INLINE_DEPTH"); rootSeen = true; depth = 1; expectKey = true; }
            else if (c === "[") { need(depth === 1, "E_INLINE_DEPTH"); depth = 2; arrayValue = true; }
            else if (c === "]") { need(depth === 2, "E_INLINE_JSON"); --depth; }
            else if (c === "}") { need(depth === 1, "E_INLINE_JSON"); --depth; }
            else if (c === ",") { if (depth === 1) expectKey = true; else if (depth === 2) arrayValue = true; }
        }
        need(rootSeen && depth === 0, "E_INLINE_JSON");
    }
    function label(value) {
        need(typeof value === "string" && value.length > 0 && value.length <= limits.labelChars
            && !/[\x00-\x1f\x7f\\%?#]/.test(value) && !/^(?:[A-Za-z][A-Za-z0-9+.-]*:|\/)/.test(value), "E_INLINE_LABEL");
    }
    function validate(payload, compiledURL, s, record, baseCharge) {
        need(payload && typeof payload === "object" && !Array.isArray(payload), "E_INLINE_MAP");
        var required = ["version", "file", "names", "sources", "sourcesContent", "mappings"];
        var allowed = required.concat(["ignoreList", "x_google_ignoreList"]), keys = Object.keys(payload);
        for (var i = 0; i < required.length; ++i) need(own(payload, required[i]), "E_INLINE_FIELDS");
        for (i = 0; i < keys.length; ++i) need(allowed.indexOf(keys[i]) !== -1, "E_INLINE_FIELDS");
        need(payload.version === 3 && typeof payload.file === "string" && payload.file.length <= 256
            && Array.isArray(payload.sources) && Array.isArray(payload.sourcesContent) && Array.isArray(payload.names)
            && payload.sources.length <= limits.sources && payload.sourcesContent.length === payload.sources.length
            && payload.names.length <= limits.names && typeof payload.mappings === "string", "E_INLINE_MAP");
        label(payload.file);
        var canonical = Object.create(null), lineCounts = [], totalLines = 0;
        for (i = 0; i < payload.sources.length; ++i) {
            label(payload.sources[i]);
            var url = WebInspector.ParsedURL.completeURL(compiledURL, payload.sources[i]);
            need(typeof url === "string" && /^(?:https?|file):\/\//.test(url) && !own(canonical, url), "E_INLINE_SOURCE_URL");
            canonical[url] = true;
            var content = payload.sourcesContent[i];
            need(typeof content === "string" && content.length > 0, "E_INLINE_CONTENT");
            var lines = 1;
            for (var j = 0; j < content.length; ++j) if (content.charAt(j) === "\n") ++lines;
            totalLines += lines; need(totalLines <= limits.sourceLinesTotal, "E_INLINE_SOURCE_LINES");
            lineCounts.push(lines);
        }
        for (i = 0; i < payload.names.length; ++i) need(typeof payload.names[i] === "string" && payload.names[i].length <= limits.nameChars, "E_INLINE_NAMES");
        var indexKeys = ["ignoreList", "x_google_ignoreList"];
        for (i = 0; i < indexKeys.length; ++i) if (own(payload, indexKeys[i])) {
            var ignored = payload[indexKeys[i]], indices = Object.create(null);
            need(Array.isArray(ignored) && ignored.length <= payload.sources.length, "E_INLINE_IGNORE");
            for (j = 0; j < ignored.length; ++j) {
                need(integer(ignored[j]) && ignored[j] >= 0 && ignored[j] < payload.sources.length && !own(indices, ignored[j]), "E_INLINE_IGNORE");
                indices[ignored[j]] = true;
            }
        }
        var fixed = baseCharge + totalLines * 16 + payload.sources.length * 256 + payload.names.length * 32;
        reserve(s, record, fixed);
        var lineLengths = [];
        for (i = 0; i < payload.sourcesContent.length; ++i) {
            var textLines = payload.sourcesContent[i].split("\n"), lengths = [];
            for (j = 0; j < textLines.length; ++j) lengths.push(textLines[j].length);
            lineLengths.push(lengths);
        }
        var map = payload.mappings, pos = 0, generatedLine = 0, column = 0, source = 0, originalLine = 0, originalColumn = 0, name = 0, segments = 0;
        var fields = [];
        while (pos < map.length) {
            if (map.charAt(pos) === ";") { need(++generatedLine < limits.generatedLines, "E_INLINE_GENERATED_LINES"); column = 0; ++pos; continue; }
            fields.length = 0;
            while (pos < map.length && map.charAt(pos) !== "," && map.charAt(pos) !== ";") {
                need(fields.length < 5, "E_INLINE_VLQ_FIELDS");
                var value = 0, scale = 1, digits = 0, digit;
                do {
                    need(pos < map.length && ++digits <= limits.vlqDigits, "E_INLINE_VLQ");
                    digit = alphabet.indexOf(map.charAt(pos++));
                    need(digit >= 0, "E_INLINE_VLQ");
                    value += (digit & 31) * scale;
                    need(value <= limits.vlqUnsigned, "E_INLINE_VLQ_OVERFLOW"); scale *= 32;
                } while (digit & 32);
                fields.push((value & 1) ? -Math.floor(value / 2) : Math.floor(value / 2));
            }
            need(fields.length === 1 || fields.length === 4 || fields.length === 5, "E_INLINE_VLQ_FIELDS");
            need(++segments <= limits.segments && fields[0] >= 0, "E_INLINE_SEGMENTS");
            column += fields[0]; need(column <= limits.column, "E_INLINE_COLUMN");
            if (fields.length >= 4) {
                source += fields[1]; originalLine += fields[2]; originalColumn += fields[3];
                need(source >= 0 && source < payload.sources.length && originalLine >= 0
                    && originalLine < lineCounts[source] && originalColumn >= 0
                    && originalColumn <= lineLengths[source][originalLine], "E_INLINE_COORDINATE");
                if (fields.length === 5) { name += fields[4]; need(name >= 0 && name < payload.names.length, "E_INLINE_NAME_INDEX"); }
            }
            if (map.charAt(pos) === ",") { ++pos; need(pos < map.length && map.charAt(pos) !== "," && map.charAt(pos) !== ";", "E_INLINE_VLQ_FIELDS"); }
        }
        reserve(s, record, fixed + segments * 48);
    }
    function callbackDiagnostic(handle) {
        var s = stateOf(handle);
        return !s || s.diagnosticUnknown ? 1023 : s.diagnosticDepth ? s.diagnosticWord : -1;
    }
    function load(url, compiledURL, callback, handle) {
        var s = stateOf(handle), record = null, callHeld = false, result = null, epoch = s ? s.epoch : -1;
        var diagnostic = {mask: 0, error: 0};
        try {
            need(current(s, epoch), "E_INLINE_STATE");
            var encoded = header(url);
            need(typeof compiledURL === "string" && compiledURL.length <= limits.labelChars
                && /^(?:https?|file):\/\//.test(compiledURL) && !/[\x00-\x1f\x7f]/.test(compiledURL), "E_INLINE_BASE");
            var key = compiledURL + "\n" + url;
            record = s.cache[key];
            if (record) {
                need(record.cached && record.model && !record.released, "E_INLINE_PENDING");
                ++record.active; callHeld = true; result = record.model;
            } else {
                need(s.records.length < limits.cacheRecordsIncludingActiveRetired, "E_INLINE_SLOTS");
                record = {key: key, charge: 0, cached: false, active: 1, released: false, model: null, bindings: 0};
                callHeld = true;
                reserve(s, record, url.length * 2 + encoded.bytes * 4);
                s.records.push(record); s.cache[key] = record;
                var text = utf8(encoded, diagnostic); preflightJSON(text);
                var payload;
                try { payload = JSON.parse(text); } catch (e) { fail("E_INLINE_JSON"); }
                validate(payload, compiledURL, s, record, record.charge);
                need(current(s, epoch), "E_INLINE_STALE");
                result = new WebInspector.SourceMap(compiledURL, payload);
                need(current(s, epoch), "E_INLINE_STALE");
                record.model = result; record.cached = true;
                models.set(result, {s: s, record: record, epoch: epoch});
            }
            s.lastError = "";
        } catch (e) {
            result = null;
            if (s) s.lastError = e && typeof e.message === "string" && own(errorCodes, e.message) ? e.message : "E_INLINE_PARSE";
            diagnostic.error = diagnosticErrorCodes[s ? s.lastError : "E_INLINE_STATE"] || 63;
        }
        // Do not catch a callback exception and invoke it for a second time.
        var previousDiagnostic = s ? s.diagnosticWord : -1, previousDepth = s ? s.diagnosticDepth : 0;
        if (s) {
            s.diagnosticDepth = previousDepth + 1;
            if (s.diagnosticDepth > 8) s.diagnosticUnknown = true;
            s.diagnosticWord = s.diagnosticUnknown ? 1023 : diagnostic.mask * 64 + diagnostic.error;
        }
        try { callback(result); }
        finally {
            if (s) { s.diagnosticWord = previousDiagnostic; s.diagnosticDepth = previousDepth; }
            if (callHeld && record && !record.released) { --record.active; retire(s, record); }
        }
    }
    return Object.freeze({create: create, beginReset: beginReset, endReset: endReset, dispose: dispose, status: status,
        isCurrent: function(handle, epoch) { return current(stateOf(handle), epoch); }, load: load,
        reserveBinding: reserveBinding, beginBinding: beginBinding, endBinding: endBinding, releaseBinding: releaseBinding,
        diagnostic: callbackDiagnostic});
})();

WebInspector.SourceMap._createInlineState = Stage0InlineMaps.create;
WebInspector.SourceMap._beginInlineReset = Stage0InlineMaps.beginReset;
WebInspector.SourceMap._endInlineReset = Stage0InlineMaps.endReset;
WebInspector.SourceMap._disposeInlineState = Stage0InlineMaps.dispose;
WebInspector.SourceMap._inlineStatus = Stage0InlineMaps.status;
Object.defineProperty(WebInspector.SourceMap, "_inlineLoaderDiagnostic", {value: Stage0InlineMaps.diagnostic, writable: false, configurable: false});
WebInspector.SourceMap._inlineCurrent = Stage0InlineMaps.isCurrent;
WebInspector.SourceMap._reserveInlineBinding = Stage0InlineMaps.reserveBinding;
WebInspector.SourceMap._beginInlineBinding = Stage0InlineMaps.beginBinding;
WebInspector.SourceMap._endInlineBinding = Stage0InlineMaps.endBinding;
WebInspector.SourceMap._releaseInlineBinding = Stage0InlineMaps.releaseBinding;
WebInspector.SourceMap.load = function(sourceMapURL, compiledURL, callback, inlineState)
{
    Stage0InlineMaps.load(sourceMapURL, compiledURL, callback, inlineState);
};

WebInspector.SourceMap.prototype = {
    /**
     * @return {string}
     */
    url: function()
    {
        return this._sourceMappingURL;
    },

   /**
     * @return {!Array.<string>}
     */
    sources: function()
    {
        return Object.keys(this._sources);
    },

    /**
     * @param {string} sourceURL
     * @return {string|undefined}
     */
    sourceContent: function(sourceURL)
    {
        return this._sourceContentByURL[sourceURL];
    },

    /**
     * @param {string} sourceURL
     * @param {!WebInspector.ResourceType} contentType
     * @return {!WebInspector.ContentProvider}
     */
    sourceContentProvider: function(sourceURL, contentType)
    {
        var sourceContent = this.sourceContent(sourceURL);
        if (sourceContent)
            return new WebInspector.StaticContentProvider(contentType, sourceContent);
        throw new Error("E_INLINE_CONTENT");
    },

    /**
     * @param {!SourceMapV3} mappingPayload
     */
    _parseMappingPayload: function(mappingPayload)
    {
        if (mappingPayload.sections)
            this._parseSections(mappingPayload.sections);
        else
            this._parseMap(mappingPayload, 0, 0);
    },

    /**
     * @param {!Array.<!SourceMapV3.Section>} sections
     */
    _parseSections: function(sections)
    {
        for (var i = 0; i < sections.length; ++i) {
            var section = sections[i];
            this._parseMap(section.map, section.offset.line, section.offset.column);
        }
    },

    /**
     * @param {number} lineNumber in compiled resource
     * @param {number} columnNumber in compiled resource
     * @return {?Array.<number|string>}
     */
    findEntry: function(lineNumber, columnNumber)
    {
        var first = 0;
        var count = this._mappings.length;
        while (count > 1) {
          var step = count >> 1;
          var middle = first + step;
          var mapping = this._mappings[middle];
          if (lineNumber < mapping[0] || (lineNumber === mapping[0] && columnNumber < mapping[1]))
              count = step;
          else {
              first = middle;
              count -= step;
          }
        }
        var entry = this._mappings[first];
        if (!first && entry && (lineNumber < entry[0] || (lineNumber === entry[0] && columnNumber < entry[1])))
            return null;
        return entry;
    },

    /**
     * @param {string} sourceURL of the originating resource
     * @param {number} lineNumber in the originating resource
     * @param {number=} span
     * @return {?Array.<*>}
     */
    findEntryReversed: function(sourceURL, lineNumber, span)
    {
        var mappings = this._reverseMappingsBySourceURL[sourceURL];
        if (!mappings)
            return null;
        var maxLineNumber = typeof span === "number" ? Math.min(lineNumber + span + 1, mappings.length) : mappings.length;
        for ( ; lineNumber < maxLineNumber; ++lineNumber) {
            var mapping = mappings[lineNumber];
            if (mapping)
                return mapping;
        }
        return null;
    },

    /**
     * @param {!SourceMapV3} map
     * @param {number} lineNumber
     * @param {number} columnNumber
     */
    _parseMap: function(map, lineNumber, columnNumber)
    {
        var sourceIndex = 0;
        var sourceLineNumber = 0;
        var sourceColumnNumber = 0;
        var nameIndex = 0;

        var sources = [];
        var originalToCanonicalURLMap = Object.create(null);
        for (var i = 0; i < map.sources.length; ++i) {
            var originalSourceURL = map.sources[i];
            var sourceRoot = map.sourceRoot || "";
            if (sourceRoot && !sourceRoot.endsWith("/"))
                sourceRoot += "/";
            var href = sourceRoot + originalSourceURL;
            var url = WebInspector.ParsedURL.completeURL(this._sourceMappingURL, href) || href;
            originalToCanonicalURLMap[originalSourceURL] = url;
            sources.push(url);
            this._sources[url] = true;

            if (map.sourcesContent && map.sourcesContent[i])
                this._sourceContentByURL[url] = map.sourcesContent[i];
        }

        var stringCharIterator = new WebInspector.SourceMap.StringCharIterator(map.mappings);
        var sourceURL = sources[sourceIndex];

        while (true) {
            if (stringCharIterator.peek() === ",")
                stringCharIterator.next();
            else {
                while (stringCharIterator.peek() === ";") {
                    lineNumber += 1;
                    columnNumber = 0;
                    stringCharIterator.next();
                }
                if (!stringCharIterator.hasNext())
                    break;
            }

            columnNumber += this._decodeVLQ(stringCharIterator);
            if (!stringCharIterator.hasNext() || this._isSeparator(stringCharIterator.peek())) {
                this._mappings.push([lineNumber, columnNumber]);
                continue;
            }

            var sourceIndexDelta = this._decodeVLQ(stringCharIterator);
            if (sourceIndexDelta) {
                sourceIndex += sourceIndexDelta;
                sourceURL = sources[sourceIndex];
            }
            sourceLineNumber += this._decodeVLQ(stringCharIterator);
            sourceColumnNumber += this._decodeVLQ(stringCharIterator);
            if (!this._isSeparator(stringCharIterator.peek()))
                nameIndex += this._decodeVLQ(stringCharIterator);

            this._mappings.push([lineNumber, columnNumber, sourceURL, sourceLineNumber, sourceColumnNumber]);
        }

        for (var i = 0; i < this._mappings.length; ++i) {
            var mapping = this._mappings[i];
            var url = mapping[2];
            if (!url)
                continue;
            if (!this._reverseMappingsBySourceURL[url])
                this._reverseMappingsBySourceURL[url] = [];
            var reverseMappings = this._reverseMappingsBySourceURL[url];
            var sourceLine = mapping[3];
            if (!reverseMappings[sourceLine])
                reverseMappings[sourceLine] = [mapping[0], mapping[1]];
        }
    },

    /**
     * @param {string} char
     * @return {boolean}
     */
    _isSeparator: function(char)
    {
        return char === "," || char === ";";
    },

    /**
     * @param {!WebInspector.SourceMap.StringCharIterator} stringCharIterator
     * @return {number}
     */
    _decodeVLQ: function(stringCharIterator)
    {
        // Read unsigned value.
        var result = 0;
        var shift = 0;
        do {
            var digit = this._base64Map[stringCharIterator.next()];
            result += (digit & this._VLQ_BASE_MASK) << shift;
            shift += this._VLQ_BASE_SHIFT;
        } while (digit & this._VLQ_CONTINUATION_MASK);

        // Fix the sign.
        var negative = result & 1;
        result >>= 1;
        return negative ? -result : result;
    },

    _VLQ_BASE_SHIFT: 5,
    _VLQ_BASE_MASK: (1 << 5) - 1,
    _VLQ_CONTINUATION_MASK: 1 << 5
}

/**
 * @constructor
 * @param {string} string
 */
WebInspector.SourceMap.StringCharIterator = function(string)
{
    this._string = string;
    this._position = 0;
}

WebInspector.SourceMap.StringCharIterator.prototype = {
    /**
     * @return {string}
     */
    next: function()
    {
        return this._string.charAt(this._position++);
    },

    /**
     * @return {string}
     */
    peek: function()
    {
        return this._string.charAt(this._position);
    },

    /**
     * @return {boolean}
     */
    hasNext: function()
    {
        return this._position < this._string.length;
    }
}
