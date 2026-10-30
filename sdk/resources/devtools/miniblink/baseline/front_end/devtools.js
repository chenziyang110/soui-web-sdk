// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(window) {

// Injected inside the existing devtools.js window closure. Private numeric observer only.
try {
    (function() {
        var sequence = 0, busy = false, lastPhase = 0, firstErrorPhase = 0, firstErrorKind = 0;
        function count(value) {
            if (Array.isArray(value)) value = value.length;
            return typeof value === "number" && isFinite(value) && value >= 0 && Math.floor(value) === value
                ? Math.min(value, 4097) : 4097;
        }
        function errorKind(error) {
            // Never read exception.name/message/stack or invoke a user getter.
            if (error === null) return 6;
            var kind = typeof error;
            return kind === "object" ? 1 : kind === "string" ? 2 : kind === "number" ? 3
                : kind === "boolean" ? 4 : kind === "undefined" ? 5 : kind === "function" ? 7 : 8;
        }
        function isPaused(message) {
            try {
                if (typeof message === "string") {
                    if (message.length > 1048576) return false;
                    message = JSON.parse(message);
                }
                if (!message || typeof message !== "object") return false;
                var descriptor = Object.getOwnPropertyDescriptor(message, "method");
                return !!descriptor && Object.prototype.hasOwnProperty.call(descriptor, "value")
                    && descriptor.value === "Debugger.paused";
            } catch (ignored) { return false; }
        }
        window.__souiPauseTraceV1 = function(phase, raw, known, target, error) {
            if (busy || sequence > 96) return;
            busy = true;
            try {
                if (sequence === 96) phase = 99;
                if (typeof phase !== "number" || Math.floor(phase) !== phase
                    || !(phase >= 1 && phase <= 17 || phase === 90 || phase === 91 || phase === 99)) return;
                if (phase < 90) lastPhase = phase;
                if ((phase === 90 || phase === 91) && !firstErrorPhase) {
                    firstErrorPhase = lastPhase || phase;
                    firstErrorKind = errorKind(error);
                }
                var a = raw === undefined ? 0 : count(raw);
                var b = known === undefined ? 0 : count(known);
                var t = target === 1 || target === 2 || target === 3 ? target : 0;
                ++sequence;
                console.log("SOUI_PAUSE_DIAG_V1|" + sequence + "|" + phase + "|" + a + "|" + b
                    + "|" + t + "|" + firstErrorPhase + "|" + firstErrorKind);
            } catch (ignored) {
                // Observation must never replace the original protocol/exception path.
            } finally { busy = false; }
        };
        window.__souiPauseTraceV1.isPaused = isPaused;
        window.__souiPauseTraceV1(17); // Bootstrap positive control only, not a paused/model/UI success.
    })();
} catch (ignored) {}

// DevToolsAPI ----------------------------------------------------------------

/**
 * @constructor
 */
function DevToolsAPIImpl()
{
    /**
     * @type {number}
     */
    this._lastCallId = 0;

    /**
     * @type {!Object.<number, function(?Object)>}
     */
    this._callbacks = {};
}

DevToolsAPIImpl.prototype = {
    /**
     * @param {number} id
     * @param {?Object} arg
     */
    embedderMessageAck: function(id, arg)
    {
        var callback = this._callbacks[id];
        delete this._callbacks[id];
        if (callback)
            callback(arg);
    },

    /**
     * @param {string} method
     * @param {!Array.<*>} args
     * @param {?function(?Object)} callback
     */
    sendMessageToEmbedder: function(method, args, callback)
    {
        var callId = ++this._lastCallId;
        if (callback)
            this._callbacks[callId] = callback;
        var message = { "id": callId, "method": method };
        if (args.length)
            message.params = args;
        DevToolsHost.sendMessageToEmbedder(JSON.stringify(message));
    },

    /**
     * @param {string} method
     * @param {!Array.<*>} args
     */
    _dispatchOnInspectorFrontendAPI: function(method, args)
    {
        var api = window["InspectorFrontendAPI"];
        api[method].apply(api, args);
    },

    // API methods below this line --------------------------------------------

    /**
     * @param {!Array.<!ExtensionDescriptor>} extensions
     */
    addExtensions: function(extensions)
    {
        // Support for legacy front-ends (<M41).
        if (window["WebInspector"].addExtensions)
            window["WebInspector"].addExtensions(extensions);
        else
            this._dispatchOnInspectorFrontendAPI("addExtensions", [extensions]);
    },

    /**
     * @param {string} url
     */
    appendedToURL: function(url)
    {
        this._dispatchOnInspectorFrontendAPI("appendedToURL", [url]);
    },

    /**
     * @param {string} url
     */
    canceledSaveURL: function(url)
    {
        this._dispatchOnInspectorFrontendAPI("canceledSaveURL", [url]);
    },

    contextMenuCleared: function()
    {
        this._dispatchOnInspectorFrontendAPI("contextMenuCleared", []);
    },

    /**
     * @param {string} id
     */
    contextMenuItemSelected: function(id)
    {
        this._dispatchOnInspectorFrontendAPI("contextMenuItemSelected", [id]);
    },

    /**
     * @param {number} count
     */
    deviceCountUpdated: function(count)
    {
        this._dispatchOnInspectorFrontendAPI("deviceCountUpdated", [count]);
    },

    /**
     * @param {!Array.<!Adb.Device>} devices
     */
    devicesUpdated: function(devices)
    {
        this._dispatchOnInspectorFrontendAPI("devicesUpdated", [devices]);
    },

    /**
     * @param {string} message
     */
    dispatchMessage: function(message)
    {
        var pausedObservation = false;
        try { pausedObservation = window.__souiPauseTraceV1.isPaused(message); } catch (ignored) {}
        if (pausedObservation) try { window.__souiPauseTraceV1(1); } catch (ignored) {}
        try {
            this._dispatchOnInspectorFrontendAPI("dispatchMessage", [message]);
        } catch (error) {
            if (pausedObservation) try { window.__souiPauseTraceV1(90, undefined, undefined, 0, error); } catch (ignored) {}
            throw error;
        }
    },

    /**
     * @param {string} messageChunk
     * @param {number} messageSize
     */
    dispatchMessageChunk: function(messageChunk, messageSize)
    {
        this._dispatchOnInspectorFrontendAPI("dispatchMessageChunk", [messageChunk, messageSize]);
    },

    enterInspectElementMode: function()
    {
        this._dispatchOnInspectorFrontendAPI("enterInspectElementMode", []);
    },

    /**
     * @param {!Array.<!{fileSystemName: string, rootURL: string, fileSystemPath: string}>} fileSystems
     */
    fileSystemsLoaded: function(fileSystems)
    {
        this._dispatchOnInspectorFrontendAPI("fileSystemsLoaded", [fileSystems]);
    },

    /**
     * @param {string} fileSystemPath
     */
    fileSystemRemoved: function(fileSystemPath)
    {
        this._dispatchOnInspectorFrontendAPI("fileSystemRemoved", [fileSystemPath]);
    },

    /**
     * @param {string} errorMessage
     * @param {!{fileSystemName: string, rootURL: string, fileSystemPath: string}} fileSystem
     */
    fileSystemAdded: function(errorMessage, fileSystem)
    {
        this._dispatchOnInspectorFrontendAPI("fileSystemAdded", [errorMessage, fileSystem]);
    },

    /**
     * @param {number} requestId
     * @param {string} fileSystemPath
     * @param {number} totalWork
     */
    indexingTotalWorkCalculated: function(requestId, fileSystemPath, totalWork)
    {
        this._dispatchOnInspectorFrontendAPI("indexingTotalWorkCalculated", [requestId, fileSystemPath, totalWork]);
    },

    /**
     * @param {number} requestId
     * @param {string} fileSystemPath
     * @param {number} worked
     */
    indexingWorked: function(requestId, fileSystemPath, worked)
    {
        this._dispatchOnInspectorFrontendAPI("indexingWorked", [requestId, fileSystemPath, worked]);
    },

    /**
     * @param {number} requestId
     * @param {string} fileSystemPath
     */
    indexingDone: function(requestId, fileSystemPath)
    {
        this._dispatchOnInspectorFrontendAPI("indexingDone", [requestId, fileSystemPath]);
    },

    /**
     * @param {{type: string, keyIdentifier: string, keyCode: number, modifiers: number}} event
     */
    keyEventUnhandled: function(event)
    {
        this._dispatchOnInspectorFrontendAPI("keyEventUnhandled", [event]);
    },

    /**
     * @param {string} url
     * @param {number} lineNumber
     * @param {number} columnNumber
     */
    revealSourceLine: function(url, lineNumber, columnNumber)
    {
        this._dispatchOnInspectorFrontendAPI("revealSourceLine", [url, lineNumber, columnNumber]);
    },

    /**
     * @param {string} url
     */
    savedURL: function(url)
    {
        this._dispatchOnInspectorFrontendAPI("savedURL", [url]);
    },

    /**
     * @param {number} requestId
     * @param {string} fileSystemPath
     * @param {!Array.<string>} files
     */
    searchCompleted: function(requestId, fileSystemPath, files)
    {
        this._dispatchOnInspectorFrontendAPI("searchCompleted", [requestId, fileSystemPath, files]);
    },

    /**
     * @param {string} tabId
     */
    setInspectedTabId: function(tabId)
    {
        // Support for legacy front-ends (<M41).
        if (window["WebInspector"].setInspectedTabId)
            window["WebInspector"].setInspectedTabId(tabId);
        else
            this._dispatchOnInspectorFrontendAPI("setInspectedTabId", [tabId]);
    },

    /**
     * @param {boolean} useSoftMenu
     */
    setUseSoftMenu: function(useSoftMenu)
    {
        this._dispatchOnInspectorFrontendAPI("setUseSoftMenu", [useSoftMenu]);
    },

    showConsole: function()
    {
        this._dispatchOnInspectorFrontendAPI("showConsole", []);
    },

    /**
     * @param {number} id
     * @param {string} chunk
     */
    streamWrite: function(id, chunk)
    {
        this._dispatchOnInspectorFrontendAPI("streamWrite", [id, chunk]);
    }
}

var DevToolsAPI = new DevToolsAPIImpl();
window.DevToolsAPI = DevToolsAPI;

// InspectorFrontendHostImpl --------------------------------------------------

/**
 * @constructor
 * @implements {InspectorFrontendHostAPI}
 */
function InspectorFrontendHostImpl()
{
}

InspectorFrontendHostImpl.prototype = {
    /**
     * @override
     * @return {string}
     */
    getSelectionBackgroundColor: function()
    {
        return DevToolsHost.getSelectionBackgroundColor();
    },

    /**
     * @override
     * @return {string}
     */
    getSelectionForegroundColor: function()
    {
        return DevToolsHost.getSelectionForegroundColor();
    },

    /**
     * @override
     * @return {string}
     */
    platform: function()
    {
        return DevToolsHost.platform();
    },

    /**
     * @override
     */
    loadCompleted: function()
    {
        DevToolsAPI.sendMessageToEmbedder("loadCompleted", [], null);
    },

    /**
     * @override
     */
    bringToFront: function()
    {
        DevToolsAPI.sendMessageToEmbedder("bringToFront", [], null);
    },

    /**
     * @override
     */
    closeWindow: function()
    {
        DevToolsAPI.sendMessageToEmbedder("closeWindow", [], null);
    },

    /**
     * @override
     * @param {boolean} isDocked
     * @param {function()} callback
     */
    setIsDocked: function(isDocked, callback)
    {
        DevToolsAPI.sendMessageToEmbedder("setIsDocked", [isDocked], callback);
    },

    /* qb_begin: aero effect */
    /**
     * @param {string} dockSide
     */
    setDockSide: function (dockSide)
    {
        DevToolsAPI.sendMessageToEmbedder("setDockSide", [dockSide], null);
    },
    /* qb_end */
    /* qb_begin: aero effect */
    /**
     * @param {bool} emulationEnabled
     */
    setEmulationEnabled: function (emulationEnabled)
    {
        DevToolsAPI.sendMessageToEmbedder("setEmulationEnabled", [emulationEnabled], null);
    },
    /* qb_end */
    /**
     * Requests inspected page to be placed atop of the inspector frontend with specified bounds.
     * @override
     * @param {{x: number, y: number, width: number, height: number}} bounds
     */
    setInspectedPageBounds: function(bounds)
    {
        DevToolsAPI.sendMessageToEmbedder("setInspectedPageBounds", [bounds], null);
    },

    /**
     * @override
     */
    inspectElementCompleted: function()
    {
        DevToolsAPI.sendMessageToEmbedder("inspectElementCompleted", [], null);
    },

    /**
     * @override
     * @param {string} url
     * @param {string} headers
     * @param {number} streamId
     * @param {function(!InspectorFrontendHostAPI.LoadNetworkResourceResult)} callback
     */
    loadNetworkResource: function(url, headers, streamId, callback)
    {
        DevToolsAPI.sendMessageToEmbedder("loadNetworkResource", [url, headers, streamId], /** @type {function(?Object)} */ (callback));
    },

    /**
     * @override
     * @param {function(!Object<string, string>)} callback
     */
    getPreferences: function(callback)
    {
        DevToolsAPI.sendMessageToEmbedder("getPreferences", [], /** @type {function(?Object)} */ (callback));
    },

    /**
     * @override
     * @param {string} name
     * @param {string} value
     */
    setPreference: function(name, value)
    {
        DevToolsAPI.sendMessageToEmbedder("setPreference", [name, value], null);
    },

    /**
     * @override
     * @param {string} name
     */
    removePreference: function(name)
    {
        DevToolsAPI.sendMessageToEmbedder("removePreference", [name], null);
    },

    /**
     * @override
     */
    clearPreferences: function()
    {
        DevToolsAPI.sendMessageToEmbedder("clearPreferences", [], null);
    },

    /**
     * @override
     * @param {string} origin
     * @param {string} script
     */
    setInjectedScriptForOrigin: function(origin, script)
    {
        DevToolsHost.setInjectedScriptForOrigin(origin, script);
    },

    /**
     * @override
     * @param {string} url
     */
    inspectedURLChanged: function(url)
    {
        DevToolsAPI.sendMessageToEmbedder("inspectedURLChanged", [url], null);
    },

    /**
     * @override
     * @param {string} text
     */
    copyText: function(text)
    {
        DevToolsHost.copyText(text);
    },

    /**
     * @override
     * @param {string} url
     */
    openInNewTab: function(url)
    {
        DevToolsAPI.sendMessageToEmbedder("openInNewTab", [url], null);
    },

    /**
     * @override
     * @param {string} url
     * @param {string} content
     * @param {boolean} forceSaveAs
     */
    save: function(url, content, forceSaveAs)
    {
        DevToolsAPI.sendMessageToEmbedder("save", [url, content, forceSaveAs], null);
    },

    /**
     * @override
     * @param {string} url
     * @param {string} content
     */
    append: function(url, content)
    {
        DevToolsAPI.sendMessageToEmbedder("append", [url, content], null);
    },

    /**
     * @override
     * @param {string} message
     */
    sendMessageToBackend: function(message)
    {
        DevToolsHost.sendMessageToBackend(message);
    },

    /**
     * @override
     * @param {string} actionName
     * @param {number} actionCode
     * @param {number} bucketSize
     */
    recordEnumeratedHistogram: function(actionName, actionCode, bucketSize)
    {
        DevToolsAPI.sendMessageToEmbedder("recordEnumeratedHistogram", [actionName, actionCode, bucketSize], null);
    },

    /**
     * @override
     */
    requestFileSystems: function()
    {
        DevToolsAPI.sendMessageToEmbedder("requestFileSystems", [], null);
    },

    /**
     * @override
     */
    addFileSystem: function()
    {
        DevToolsAPI.sendMessageToEmbedder("addFileSystem", [], null);
    },

    /**
     * @override
     * @param {string} fileSystemPath
     */
    removeFileSystem: function(fileSystemPath)
    {
        DevToolsAPI.sendMessageToEmbedder("removeFileSystem", [fileSystemPath], null);
    },

    /**
     * @override
     * @param {string} fileSystemId
     * @param {string} registeredName
     * @return {?DOMFileSystem}
     */
    isolatedFileSystem: function(fileSystemId, registeredName)
    {
        return DevToolsHost.isolatedFileSystem(fileSystemId, registeredName);
    },

    /**
     * @override
     * @param {!FileSystem} fileSystem
     */
    upgradeDraggedFileSystemPermissions: function(fileSystem)
    {
        DevToolsHost.upgradeDraggedFileSystemPermissions(fileSystem);
    },

    /**
     * @override
     * @param {number} requestId
     * @param {string} fileSystemPath
     */
    indexPath: function(requestId, fileSystemPath)
    {
        DevToolsAPI.sendMessageToEmbedder("indexPath", [requestId, fileSystemPath], null);
    },

    /**
     * @override
     * @param {number} requestId
     */
    stopIndexing: function(requestId)
    {
        DevToolsAPI.sendMessageToEmbedder("stopIndexing", [requestId], null);
    },

    /**
     * @override
     * @param {number} requestId
     * @param {string} fileSystemPath
     * @param {string} query
     */
    searchInPath: function(requestId, fileSystemPath, query)
    {
        DevToolsAPI.sendMessageToEmbedder("searchInPath", [requestId, fileSystemPath, query], null);
    },

    /**
     * @override
     * @return {number}
     */
    zoomFactor: function()
    {
        return DevToolsHost.zoomFactor();
    },

    /**
     * @override
     */
    zoomIn: function()
    {
        DevToolsAPI.sendMessageToEmbedder("zoomIn", [], null);
    },

    /**
     * @override
     */
    zoomOut: function()
    {
        DevToolsAPI.sendMessageToEmbedder("zoomOut", [], null);
    },

    /**
     * @override
     */
    resetZoom: function()
    {
        DevToolsAPI.sendMessageToEmbedder("resetZoom", [], null);
    },

    /**
     * @override
     * @param {string} shortcuts
     */
    setWhitelistedShortcuts: function(shortcuts)
    {
        DevToolsAPI.sendMessageToEmbedder("setWhitelistedShortcuts", [shortcuts], null);
    },

    /**
     * @override
     * @return {boolean}
     */
    isUnderTest: function()
    {
        return DevToolsHost.isUnderTest();
    },

    /**
     * @override
     * @param {boolean} enabled
     */
    setDevicesUpdatesEnabled: function(enabled)
    {
        DevToolsAPI.sendMessageToEmbedder("setDevicesUpdatesEnabled", [enabled], null);
    },

    /**
     * @override
     * @param {number} x
     * @param {number} y
     * @param {!Array.<!InspectorFrontendHostAPI.ContextMenuDescriptor>} items
     * @param {!Document} document
     */
    showContextMenuAtPoint: function(x, y, items, document)
    {
        DevToolsHost.showContextMenuAtPoint(x, y, items, document);
    },

    /**
     * @override
     * @return {boolean}
     */
    isHostedMode: function()
    {
        return DevToolsHost.isHostedMode();
    },

    // Backward-compatible methods below this line --------------------------------------------

    /**
     * Support for legacy front-ends (<M41).
     * @return {string}
     */
    port: function()
    {
        return "unknown";
    },

    /**
     * Support for legacy front-ends (<M38).
     * @param {number} zoomFactor
     */
    setZoomFactor: function(zoomFactor)
    {
    },

    /**
     * Support for legacy front-ends (<M34).
     */
    sendMessageToEmbedder: function()
    {
    },

    /**
     * Support for legacy front-ends (<M34).
     * @param {string} dockSide
     */
    requestSetDockSide: function(dockSide)
    {
        DevToolsAPI.sendMessageToEmbedder("setIsDocked", [dockSide !== "undocked"], null);
    },

    /**
     * Support for legacy front-ends (<M34).
     * @return {boolean}
     */
    supportsFileSystems: function()
    {
        return true;
    },

    /**
     * Support for legacy front-ends (<M28).
     * @return {boolean}
     */
    canInspectWorkers: function()
    {
        return true;
    },

    /**
     * Support for legacy front-ends (<M28).
     * @return {boolean}
     */
    canSaveAs: function()
    {
        return true;
    },

    /**
     * Support for legacy front-ends (<M28).
     * @return {boolean}
     */
    canSave: function()
    {
        return true;
    },

    /**
     * Support for legacy front-ends (<M28).
     */
    loaded: function()
    {
    },

    /**
     * Support for legacy front-ends (<M28).
     * @return {string}
     */
    hiddenPanels: function()
    {
        return "";
    },

    /**
     * Support for legacy front-ends (<M28).
     * @return {string}
     */
    localizedStringsURL: function()
    {
        return "";
    },

    /**
     * Support for legacy front-ends (<M28).
     * @param {string} url
     */
    close: function(url)
    {
    },

    /**
     * Support for legacy front-ends (<M44).
     * @param {number} actionCode
     */
    recordActionTaken: function(actionCode)
    {
        this.recordEnumeratedHistogram("DevTools.ActionTaken", actionCode, 100);
    },

    /**
     * Support for legacy front-ends (<M44).
     * @param {number} panelCode
     */
    recordPanelShown: function(panelCode)
    {
        this.recordEnumeratedHistogram("DevTools.PanelShown", panelCode, 20);
    }
}

window.InspectorFrontendHost = new InspectorFrontendHostImpl();

// DevToolsApp ---------------------------------------------------------------

/**
 * @suppressGlobalPropertiesCheck
 */
function installBackwardsCompatibility()
{
    /**
     * @this {CSSStyleDeclaration}
     */
    function getValue(property)
    {
        // Note that |property| comes from another context, so we can't use === here.
        if (property == "padding-left") {
            return {
                /**
                 * @suppressReceiverCheck
                 * @this {Object}
                 */
                getFloatValue: function() { return this.__paddingLeft; },
                __paddingLeft: parseFloat(this.paddingLeft)
            };
        }
        throw new Error("getPropertyCSSValue is undefined");
    }

    // Support for legacy (<M41) frontends. Remove in M45.
    window.CSSStyleDeclaration.prototype.getPropertyCSSValue = getValue;

    function CSSPrimitiveValue()
    {
    }
    CSSPrimitiveValue.CSS_PX = 5;
    window.CSSPrimitiveValue = CSSPrimitiveValue;

    // Support for legacy (<M44) frontends. Remove in M48.
    var styleElement = window.document.createElement("style");
    styleElement.type = "text/css";
    styleElement.textContent = "html /deep/ * { min-width: 0; min-height: 0; }";
    window.document.head.appendChild(styleElement);
}

function windowLoaded()
{
    window.removeEventListener("DOMContentLoaded", windowLoaded, false);
    installBackwardsCompatibility();
}

if (window.document.head && (window.document.readyState === "complete" || window.document.readyState === "interactive"))
    installBackwardsCompatibility();
else
    window.addEventListener("DOMContentLoaded", windowLoaded, false);

// UITests ------------------------------------------------------------------

if (window.domAutomationController) {
    var uiTests = {};

    uiTests._tryRun = function()
    {
        if (uiTests._testSuite && uiTests._pendingTestName) {
            var name = uiTests._pendingTestName;
            delete uiTests._pendingTestName;
            uiTests._testSuite.runTest(name);
        }
    }

    uiTests.runTest = function(name)
    {
        uiTests._pendingTestName = name;
        uiTests._tryRun();
    };

    uiTests.testSuiteReady = function(testSuiteConstructor, testBase)
    {
        uiTests._testSuite = testSuiteConstructor(window.domAutomationController);
        uiTests._tryRun();
    };

    window.uiTests = uiTests;
}

})(window);
