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
 * @implements {WebInspector.DebuggerSourceMapping}
 * @param {!WebInspector.DebuggerModel} debuggerModel
 * @param {!WebInspector.Workspace} workspace
 * @param {!WebInspector.NetworkMapping} networkMapping
 * @param {!WebInspector.NetworkProject} networkProject
 * @param {!WebInspector.DebuggerWorkspaceBinding} debuggerWorkspaceBinding
 */
var Stage0InlinePublicationDiagnostics = (function() {
    "use strict";
    var owners = typeof WeakMap === "function" ? new WeakMap() : null;
    function create(owner) {
        try {
            if (!owners) return;
            var sourceMap = WebInspector.SourceMap, status = sourceMap._inlineStatus;
            var reader = Object.getOwnPropertyDescriptor(sourceMap, "_inlineLoaderDiagnostic");
            var handle = owner._inlineState, snapshot = status(handle);
            owners.set(owner, {handle: handle, target: owner._target, model: owner._debuggerModel, status: status,
                loader: reader && reader.value, epoch: snapshot.epoch, rows: new WeakMap(), count: 0,
                unknown: !reader || typeof reader.value !== "function", disposed: false, resetSeen: false});
        } catch (_) { /* Missing owner is read as unknown; never alter original constructor propagation. */ }
    }
    function state(owner, epoch) {
        var s = owners && owners.get(owner);
        return s && !s.disposed && s.epoch === epoch ? s : null;
    }
    function begin(owner, script, epoch) {
        var s = state(owner, epoch); if (!s || s.unknown) return;
        try {
            var row = s.rows.get(script);
            if (!row) {
                if (s.count >= 8) { s.unknown = true; return; }
                row = {phase: 3, error: 0, mask: 0, active: false}; s.rows.set(script, row); ++s.count;
            }
            if (row.active) { s.unknown = true; return; }
            row.phase = 3; row.error = 0; row.mask = 0; row.active = true;
        } catch (_) { s.unknown = true; }
    }
    function note(owner, script, epoch, phase) {
        var s = state(owner, epoch); if (!s || s.unknown) return;
        try { var row = s.rows.get(script); if (row) row.phase = phase; }
        catch (_) { s.unknown = true; }
    }
    function callback(owner, script, epoch, model) {
        var s = state(owner, epoch); if (!s || s.unknown) return;
        try {
            var row = s.rows.get(script); if (!row) return;
            var value = s.loader(s.handle); row.phase = model ? 6 : 5;
            if (value === -1) { row.error = 59; row.mask = 0; return; }
            if (typeof value !== "number" || value % 1 !== 0 || value < 0 || value >= 1023
                || value % 64 > 39) { s.unknown = true; return; }
            row.error = value % 64; row.mask = Math.floor(value / 64);
        } catch (_) { s.unknown = true; }
    }
    function rollback(owner, script, epoch, caught) {
        var s = state(owner, epoch); if (!s || s.unknown) return;
        try { var row = s.rows.get(script); if (row && (caught || row.error !== 61)) row.error = caught ? 61 : 60; }
        catch (_) { s.unknown = true; }
    }
    function end(owner, script, epoch) {
        var s = state(owner, epoch); if (!s) return;
        try { var row = s.rows.get(script); if (row) row.active = false; }
        catch (_) { s.unknown = true; }
    }
    function reset(owner, disposed) {
        var s = owners && owners.get(owner); if (!s) return;
        try { s.rows = new WeakMap(); s.count = 0; s.epoch = s.status(s.handle).epoch; s.resetSeen = true; s.disposed = s.disposed || disposed; }
        catch (_) { s.unknown = true; }
    }
    function read(owner, script, index) {
        var s = owners && owners.get(owner);
        if (!s || s.unknown || s.disposed || (index !== 0 && index !== 1)) return 65535;
        try {
            var handle = Object.getOwnPropertyDescriptor(owner, "_inlineState"), target = Object.getOwnPropertyDescriptor(owner, "_target"), model = Object.getOwnPropertyDescriptor(owner, "_debuggerModel");
            if (!handle || handle.value !== s.handle || !target || target.value !== s.target || !model || model.value !== s.model) { s.unknown = true; return 65535; }
            var snapshot = s.status(s.handle);
            if (snapshot.epoch !== s.epoch || snapshot.disposed || snapshot.resetting) return 65535;
            var row = s.rows.get(script), phase = row ? row.phase : 0, error = row ? row.error : s.resetSeen ? 58 : 0, mask = row ? row.mask : 0;
            return index + phase * 4 + error * 64 + mask * 4096;
        } catch (_) { s.unknown = true; return 65535; }
    }
    return Object.freeze({create: create, begin: begin, note: note, callback: callback, rollback: rollback, end: end, reset: reset, read: read});
})();

WebInspector.CompilerScriptMapping = function(debuggerModel, workspace, networkMapping, networkProject, debuggerWorkspaceBinding)
{
    this._target = debuggerModel.target();
    this._debuggerModel = debuggerModel;
    this._inlineState = WebInspector.SourceMap._createInlineState();
    this._inlineSubscriptions = new Map();
    this._inlineSources = new Map();
    this._inlineUis = new Map();
    this._inlineMapIdentity = new Map();
    this._inlineBindingError = "";
    Stage0InlinePublicationDiagnostics.create(this);
    this._workspace = workspace;
    this._workspace.addEventListener(WebInspector.Workspace.Events.UISourceCodeAdded, this._uiSourceCodeAddedToWorkspace, this);
    this._networkMapping = networkMapping;
    this._networkProject = networkProject;
    this._debuggerWorkspaceBinding = debuggerWorkspaceBinding;

    /** @type {!Object.<string, !WebInspector.SourceMap>} */
    this._sourceMapForSourceMapURL = Object.create(null);
    /** @type {!Object.<string, !Array.<function(?WebInspector.SourceMap)>>} */
    this._pendingSourceMapLoadingCallbacks = Object.create(null);
    /** @type {!Object.<string, !WebInspector.SourceMap>} */
    this._sourceMapForScriptId = Object.create(null);
    /** @type {!Map.<!WebInspector.SourceMap, !WebInspector.Script>} */
    this._scriptForSourceMap = new Map();
    /** @type {!Map.<string, !WebInspector.SourceMap>} */
    this._sourceMapForURL = new Map();
    /** @type {!Map.<string, !WebInspector.UISourceCode>} */
    this._stubUISourceCodes = new Map();

    this._stubProjectID = "compiler-script-project";
    this._stubProjectDelegate = new WebInspector.ContentProviderBasedProjectDelegate(this._workspace, this._stubProjectID, WebInspector.projectTypes.Service);
    debuggerModel.addEventListener(WebInspector.DebuggerModel.Events.GlobalObjectCleared, this._debuggerReset, this);
}

Object.defineProperty(WebInspector.CompilerScriptMapping, "_inlineDiagnosticWord", {value: Stage0InlinePublicationDiagnostics.read, writable: false, configurable: false});

WebInspector.CompilerScriptMapping.prototype = {
    /**
     * @param {!WebInspector.DebuggerModel.Location} rawLocation
     * @return {boolean}
     */
    mapsToSourceCode: function(rawLocation)
    {
        var model = this._sourceMapForScriptId[rawLocation.scriptId];
        if (!model) return true;
        var epoch = WebInspector.SourceMap._inlineStatus(this._inlineState).epoch;
        if (!this._inlineMapCurrent(model, epoch)) return false;
        var row = model.findEntry(rawLocation.lineNumber, rawLocation.columnNumber || 0);
        return !!(row && row.length >= 5 && this._inlineEntryCurrent(this._inlineSources.get(row[2])));
    },

    /**
     * @override
     * @param {!WebInspector.DebuggerModel.Location} rawLocation
     * @return {?WebInspector.UILocation}
     */
    rawLocationToUILocation: function(rawLocation)
    {
        var epoch = WebInspector.SourceMap._inlineStatus(this._inlineState).epoch;
        var model = this._sourceMapForScriptId[rawLocation.scriptId], script = this._debuggerModel.scriptForId(rawLocation.scriptId);
        if (!model || !script || !this._inlineCurrent(script, epoch) || !this._inlineMapCurrent(model, epoch)) return null;
        var row = model.findEntry(rawLocation.lineNumber, rawLocation.columnNumber || 0);
        if (!row || row.length < 5) return null;
        var entry = this._inlineSources.get(row[2]);
        if (!entry || !WebInspector.SourceMap._beginInlineBinding(this._inlineState, entry.lease)) return null;
        try {
            if (!this._inlineEntryCurrent(entry) || entry.model.sourceContent(entry.url) !== model.sourceContent(entry.url)) return null;
            var location = entry.ui.uiLocation(row[3], row[4]);
            return this._inlineEntryCurrent(entry) && this._inlineCurrent(script, epoch) ? location : null;
        } finally { WebInspector.SourceMap._endInlineBinding(entry.lease); }
    },

    /**
     * @override
     * @param {!WebInspector.UISourceCode} uiSourceCode
     * @param {number} lineNumber
     * @param {number} columnNumber
     * @return {?WebInspector.DebuggerModel.Location}
     */
    uiLocationToRawLocation: function(uiSourceCode, lineNumber, columnNumber)
    {
        var entry = this._inlineUis.get(uiSourceCode);
        if (!entry || !WebInspector.SourceMap._beginInlineBinding(this._inlineState, entry.lease)) return null;
        try {
            if (!this._inlineEntryCurrent(entry)) return null;
            var model = this._sourceMapForURL.get(entry.url), epoch = entry.epoch;
            if (!model || !this._inlineMapCurrent(model, epoch)) return null;
            var script = this._scriptForSourceMap.get(model), row = model.findEntryReversed(entry.url, lineNumber, 5);
            if (!row) return null;
            var location = this._debuggerModel.createRawLocation(script, row[0], row[1]);
            return this._inlineEntryCurrent(entry) && this._inlineCurrent(script, epoch) ? location : null;
        } finally { WebInspector.SourceMap._endInlineBinding(entry.lease); }
    },

    /**
     * @param {!WebInspector.Script} script
     */
    addScript: function(script)
    {
        var epoch = WebInspector.SourceMap._inlineStatus(this._inlineState).epoch;
        if (!this._inlineCurrent(script, epoch)) return;
        if (!script.sourceMapURL) {
            if (this._inlineSubscriptions.has(script) || this._inlineSubscriptions.size >= 8) return;
            var listener = function(event) {
                var saved = this._inlineSubscriptions.get(script);
                if (saved !== listener) return;
                this._inlineSubscriptions.delete(script);
                script.removeEventListener(WebInspector.Script.Events.SourceMapURLAdded, listener, this);
                if (event.target === script && this._inlineCurrent(script, epoch) && script.sourceMapURL)
                    this._processScript(script);
            }.bind(this);
            this._inlineSubscriptions.set(script, listener);
            script.addEventListener(WebInspector.Script.Events.SourceMapURLAdded, listener, this);
            return;
        }
        this._processScript(script);
    },

    /**
     * @param {!WebInspector.Event} event
     */
    _sourceMapURLAdded: function(event)
    {
        var script = /** @type {!WebInspector.Script} */ (event.target);
        if (!script.sourceMapURL)
            return;
        this._processScript(script);
    },

    /**
     * @param {!WebInspector.Script} script
     */
    _inlineMapCurrent: function(model, epoch)
    {
        var identity = this._inlineMapIdentity.get(model);
        return identity && identity.epoch === epoch && this._inlineCurrent(identity.script, epoch)
            && identity.script.sourceURL === identity.sourceURL && identity.script.sourceMapURL === identity.sourceMapURL;
    },

    _inlineEntryMatches: function(entry, requireCurrent)
    {
        try {
        if (!entry || !entry.ui || !entry.provider || !entry.project || !entry.delegate) return false;
        if (requireCurrent && (!WebInspector.SourceMap._inlineCurrent(this._inlineState, entry.epoch)
            || this._inlineSources.get(entry.url) !== entry || entry.phase === "cancelled")) return false;
        if (this._networkMapping.hasMappingForURL(entry.url)) return false;
        if (this._networkMapping.uiSourceCodeForURL(entry.url, this._target) !== entry.ui) return false;
        var ui = entry.ui, project = ui.project();
        var content = entry.model.sourceContent(entry.url);
        var valid = project === entry.project && project.id() === entry.projectId
            && project._projectDelegate === entry.delegate && entry.delegate._target === this._target
            && this._networkProject._projectDelegates[entry.projectId] === entry.delegate
            && ui.path() === entry.path && entry.delegate._contentProviders[entry.path] === entry.provider
            && entry.provider._content === content
            && entry.provider.requestContent === WebInspector.StaticContentProvider.prototype.requestContent
            && (ui._content === undefined || ui._content === null || ui._content === content)
            && (!ui._contentLoaded || ui._content === content)
            && typeof ui._workingCopy === "undefined" && typeof ui._workingCopyGetter === "undefined";
        return valid && (!requireCurrent || (WebInspector.SourceMap._inlineCurrent(this._inlineState, entry.epoch)
            && this._inlineSources.get(entry.url) === entry));
        } catch (error) { return false; }
    },

    _inlineEntryCurrent: function(entry)
    {
        if (!entry || entry.phase !== "committed" || !WebInspector.SourceMap._beginInlineBinding(this._inlineState, entry.lease)) return false;
        try { return this._inlineMapCurrent(entry.model, entry.epoch) && this._inlineEntryMatches(entry, true); }
        finally { WebInspector.SourceMap._endInlineBinding(entry.lease); }
    },

    _inlineCurrent: function(script, epoch)
    {
        return WebInspector.SourceMap._inlineCurrent(this._inlineState, epoch)
            && script.target() === this._target && this._debuggerModel.scriptForId(script.scriptId) === script
            && WebInspector.SourceMap._inlineCurrent(this._inlineState, epoch);
    },

    _inlineMetadataCurrent: function(script, epoch, sourceURL, sourceMapURL)
    {
        return this._inlineCurrent(script, epoch) && script.sourceURL === sourceURL && script.sourceMapURL === sourceMapURL;
    },

    _processScript: function(script)
    {
        var epoch = WebInspector.SourceMap._inlineStatus(this._inlineState).epoch;
        var expectedSourceURL = script.sourceURL, expectedMapURL = script.sourceMapURL;
        if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
        Stage0InlinePublicationDiagnostics.begin(this, script, epoch);
        var url = expectedSourceURL;
        var splitURL = WebInspector.ParsedURL.splitURLIntoPathComponents(url);
        var parentPath = splitURL.slice(1, -1).join("/");
        var name = splitURL.peekLast() || "";
        var uiSourceCodePath = this._stubProjectDelegate.addContentProvider(parentPath, name, url, url,
            new WebInspector.StaticContentProvider(WebInspector.resourceTypes.Script, "// Inline map validation in progress.", url));
        if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
        var stubUISourceCode = this._workspace.uiSourceCode(this._stubProjectID, uiSourceCodePath);
        if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
        this._stubUISourceCodes.set(script.scriptId, stubUISourceCode);
        this._debuggerWorkspaceBinding.pushSourceMapping(script, this);
        if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
        this._loadSourceMapForScript(script, this._sourceMapLoaded.bind(this, script, uiSourceCodePath, epoch, expectedSourceURL, expectedMapURL));
    },

    /**
     * @param {!WebInspector.Script} script
     * @param {string} uiSourceCodePath
     * @param {?WebInspector.SourceMap} sourceMap
     */
    _sourceMapLoaded: function(script, uiSourceCodePath, epoch, expectedSourceURL, expectedMapURL, sourceMap)
    {
        Stage0InlinePublicationDiagnostics.callback(this, script, epoch, sourceMap);
        try {
        if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
        this._stubUISourceCodes.delete(script.scriptId);
        this._stubProjectDelegate.removeFile(uiSourceCodePath);
        if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
        if (!sourceMap) {
            this._debuggerWorkspaceBinding.updateLocations(script);
            return;
        }
        var entries = [], created = [], borrowed = [], accepted = false;
        var hadOwner = this._scriptForSourceMap.has(sourceMap);
        try {
            Stage0InlinePublicationDiagnostics.note(this, script, epoch, 7);
            var sourceURLs = sourceMap.sources();
            for (var i = 0; i < sourceURLs.length; ++i) {
                if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
                var url = sourceURLs[i], entry = this._inlineSources.get(url);
                if (entry) {
                    if (entry.phase !== "committed" || entry.epoch !== epoch
                        || entry.script !== script || entry.model !== sourceMap
                        || entry.sourceURL !== expectedSourceURL || entry.sourceMapURL !== expectedMapURL
                        || entry.model.sourceContent(url) !== sourceMap.sourceContent(url)
                        || !WebInspector.SourceMap._beginInlineBinding(this._inlineState, entry.lease))
                        throw new Error("E_INLINE_CONTENT_IDENTITY");
                    borrowed.push(entry);
                    if (!this._inlineEntryMatches(entry, true)) throw new Error("E_INLINE_CONTENT_IDENTITY");
                } else {
                    // Lookup is in-memory. Unknown providers are never read to manufacture identity.
                    if (this._networkMapping.hasMappingForURL(url)) throw new Error("E_INLINE_CONTENT_IDENTITY");
                    if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
                    if (this._networkMapping.uiSourceCodeForURL(url, this._target)) throw new Error("E_INLINE_CONTENT_IDENTITY");
                    if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
                    var lease = WebInspector.SourceMap._reserveInlineBinding(this._inlineState, epoch, sourceMap);
                    entry = {url: url, epoch: epoch, model: sourceMap, script: script, sourceURL: expectedSourceURL,
                        sourceMapURL: expectedMapURL, lease: lease, phase: "creating"};
                    created.push(entry); this._inlineSources.set(url, entry);
                    entry.provider = sourceMap.sourceContentProvider(url, WebInspector.resourceTypes.Script);
                    entry.ui = this._networkProject.addFileForURL(url, entry.provider, script.isContentScript());
                    if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL) || this._inlineSources.get(url) !== entry) return;
                    if (!entry.ui) throw new Error("E_INLINE_CONTENT_IDENTITY");
                    entry.project = entry.ui.project(); entry.projectId = entry.project.id(); entry.path = entry.ui.path();
                    entry.delegate = this._networkProject._projectDelegates[entry.projectId];
                    entry.phase = "prepared";
                    if (!this._inlineEntryMatches(entry, true)) throw new Error("E_INLINE_CONTENT_IDENTITY");
                }
                if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
                entries.push(entry);
            }
            Stage0InlinePublicationDiagnostics.note(this, script, epoch, 8);
            // Commit associations only after every URL/content/UI identity passed.
            for (i = 0; i < entries.length; ++i) {
                if (!this._inlineEntryMatches(entries[i], true) || !this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL))
                    throw new Error("E_INLINE_CONTENT_IDENTITY");
            }
            for (i = 0; i < created.length; ++i) {
                created[i].phase = "committed"; this._inlineUis.set(created[i].ui, created[i]);
                this._sourceMapForURL.set(created[i].url, sourceMap);
            }
            if (!hadOwner) {
                this._scriptForSourceMap.set(sourceMap, script);
                this._inlineMapIdentity.set(sourceMap, {script: script, sourceURL: expectedSourceURL,
                    sourceMapURL: expectedMapURL, epoch: epoch});
            }
            this._sourceMapForScriptId[script.scriptId] = sourceMap;
            Stage0InlinePublicationDiagnostics.note(this, script, epoch, 9);
            for (i = 0; i < entries.length; ++i) {
                Stage0InlinePublicationDiagnostics.note(this, script, epoch, 10);
                if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL) || !this._bindUISourceCode(entries[i].ui)) return;
            }
            if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
            Stage0InlinePublicationDiagnostics.note(this, script, epoch, 11);
            this._debuggerWorkspaceBinding.updateLocations(script);
            if (!this._inlineMetadataCurrent(script, epoch, expectedSourceURL, expectedMapURL)) return;
            this._inlineBindingError = ""; accepted = true;
            Stage0InlinePublicationDiagnostics.note(this, script, epoch, 12);
        } catch (error) {
            Stage0InlinePublicationDiagnostics.rollback(this, script, epoch, true);
            if (this._inlineCurrent(script, epoch)) this._inlineBindingError = "E_INLINE_CONTENT_IDENTITY";
        } finally {
            if (!accepted) {
                Stage0InlinePublicationDiagnostics.rollback(this, script, epoch, false);
                if (this._inlineCurrent(script, epoch)) this._inlineBindingError = "E_INLINE_CONTENT_IDENTITY";
                if (this._sourceMapForScriptId[script.scriptId] === sourceMap) delete this._sourceMapForScriptId[script.scriptId];
                if (!hadOwner && this._scriptForSourceMap.get(sourceMap) === script) {
                    this._scriptForSourceMap.delete(sourceMap); this._inlineMapIdentity.delete(sourceMap);
                }
                for (var j = 0; j < created.length; ++j) {
                    var abandoned = created[j]; abandoned.phase = "cancelled";
                    if (this._inlineSources.get(abandoned.url) === abandoned) this._inlineSources.delete(abandoned.url);
                    if (this._inlineUis.get(abandoned.ui) === abandoned) this._inlineUis.delete(abandoned.ui);
                    if (this._sourceMapForURL.get(abandoned.url) === sourceMap) this._sourceMapForURL.delete(abandoned.url);
                    WebInspector.SourceMap._releaseInlineBinding(abandoned.lease);
                }
            }
            for (var k = 0; k < created.length; ++k) WebInspector.SourceMap._endInlineBinding(created[k].lease);
            for (k = 0; k < borrowed.length; ++k) WebInspector.SourceMap._endInlineBinding(borrowed[k].lease);
        }
        } finally { Stage0InlinePublicationDiagnostics.end(this, script, epoch); }
    },

    /**
     * @override
     * @return {boolean}
     */
    isIdentity: function()
    {
        return false;
    },

    /**
     * @override
     * @param {!WebInspector.UISourceCode} uiSourceCode
     * @param {number} lineNumber
     * @return {boolean}
     */
    uiLineHasMapping: function(uiSourceCode, lineNumber)
    {
        var entry = this._inlineUis.get(uiSourceCode);
        if (!entry || !this._inlineEntryCurrent(entry)) return false;
        var model = this._sourceMapForURL.get(entry.url);
        return !!(model && this._inlineMapCurrent(model, entry.epoch) && model.findEntryReversed(entry.url, lineNumber, 0));
    },

    /**
     * @param {!WebInspector.UISourceCode} uiSourceCode
     */
    _bindUISourceCode: function(uiSourceCode)
    {
        var entry = this._inlineUis.get(uiSourceCode);
        if (!entry || !WebInspector.SourceMap._beginInlineBinding(this._inlineState, entry.lease)) return false;
        try {
            if (!this._inlineEntryCurrent(entry)) return false;
            this._debuggerWorkspaceBinding.setSourceMapping(this._target, uiSourceCode, this);
            return this._inlineEntryCurrent(entry);
        } finally { WebInspector.SourceMap._endInlineBinding(entry.lease); }
    },

    /**
     * @param {!WebInspector.UISourceCode} uiSourceCode
     */
    _unbindUISourceCode: function(uiSourceCode)
    {
        this._debuggerWorkspaceBinding.setSourceMapping(this._target, uiSourceCode, null);
    },

    /**
     * @param {!WebInspector.Event} event
     */
    _uiSourceCodeAddedToWorkspace: function(event)
    {
        var entry = this._inlineUis.get(event.data);
        if (entry && this._inlineEntryCurrent(entry)) this._bindUISourceCode(event.data);
    },

    /**
     * @param {!WebInspector.Script} script
     * @param {function(?WebInspector.SourceMap)} callback
     */
    _loadSourceMapForScript: function(script, callback)
    {
        var epoch = WebInspector.SourceMap._inlineStatus(this._inlineState).epoch;
        Stage0InlinePublicationDiagnostics.note(this, script, epoch, 4);
        if (!this._inlineCurrent(script, epoch)) { callback(null); return; }
        var scriptURL = WebInspector.ParsedURL.completeURL(this._target.resourceTreeModel.inspectedPageURL(), script.sourceURL);
        if (!this._inlineCurrent(script, epoch) || !scriptURL) { callback(null); return; }
        WebInspector.SourceMap.load(script.sourceMapURL, scriptURL, callback, this._inlineState);
    },

    _debuggerReset: function()
    {
        WebInspector.SourceMap._beginInlineReset(this._inlineState);
        Stage0InlinePublicationDiagnostics.reset(this, false);
        var entries = Array.from(this._inlineSources.values());
        try {
            this._inlineSources.clear(); this._inlineUis.clear(); this._inlineMapIdentity.clear();
            this._sourceMapForSourceMapURL = Object.create(null);
            this._pendingSourceMapLoadingCallbacks = Object.create(null);
            this._sourceMapForScriptId = Object.create(null);
            this._scriptForSourceMap.clear();
            this._sourceMapForURL.clear();
            this._stubUISourceCodes.clear();
            var subscriptions = Array.from(this._inlineSubscriptions.entries());
            this._inlineSubscriptions.clear();
            for (var j = 0; j < subscriptions.length; ++j)
                subscriptions[j][0].removeEventListener(WebInspector.Script.Events.SourceMapURLAdded, subscriptions[j][1], this);
            this._stubProjectDelegate.reset();
            for (var i = 0; i < entries.length; ++i) {
                if (this._inlineEntryMatches(entries[i], false)) this._unbindUISourceCode(entries[i].ui);
                if (this._inlineEntryMatches(entries[i], false)) entries[i].delegate.removeFile(entries[i].path);
            }
        } finally {
            for (var j = 0; j < entries.length; ++j) WebInspector.SourceMap._releaseInlineBinding(entries[j].lease);
            WebInspector.SourceMap._endInlineReset(this._inlineState);
        }
    },

    dispose: function()
    {
        WebInspector.SourceMap._disposeInlineState(this._inlineState);
        Stage0InlinePublicationDiagnostics.reset(this, true);
        try { this._debuggerReset(); }
        finally {
            this._debuggerModel.removeEventListener(WebInspector.DebuggerModel.Events.GlobalObjectCleared, this._debuggerReset, this);
            this._workspace.removeEventListener(WebInspector.Workspace.Events.UISourceCodeAdded, this._uiSourceCodeAddedToWorkspace, this);
        }
    }
}
