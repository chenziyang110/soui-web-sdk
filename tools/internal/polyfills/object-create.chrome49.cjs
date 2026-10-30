'use strict';
// SOUI compatibility builds target Chrome 49, which supplies native Object.create.
// Keep that capability mandatory; do not retain core-js's old IE frame/ActiveX fallback.
var nativeCreate = Object.create;
if (typeof nativeCreate !== 'function') {
  throw new Error('SOUI_CHROME49_OBJECT_CREATE_REQUIRED');
}
module.exports = nativeCreate;
