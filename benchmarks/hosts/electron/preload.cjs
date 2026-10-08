'use strict';

const {contextBridge, ipcRenderer} = require('electron');

contextBridge.exposeInMainWorld('benchmarkHost', Object.freeze({
  kind: 'electron',
  writeResult: result => ipcRenderer.invoke('benchmark:write-result', result),
}));
