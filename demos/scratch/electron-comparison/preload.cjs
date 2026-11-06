// Comparison-only transport. This is not an Electron implementation of the SDK.
const {contextBridge, ipcRenderer} = require('electron');
if (process.isMainFrame) {
  contextBridge.exposeInMainWorld('__souiNativePostMessage', envelope => {
    if (typeof envelope !== 'string' || envelope.length > 65536)
      return Promise.reject(new Error('Invalid comparison request.'));
    return ipcRenderer.invoke('scratch-comparison:request', envelope);
  });
}
