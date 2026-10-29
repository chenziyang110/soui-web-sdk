'use strict';

// Delivery identity accepts only the project's three explicit release channels.
// A stable version is an identity, never evidence of production acceptance.
const pattern = /^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-(preview|rc)\.(0|[1-9][0-9]*))?$/;
function isVersion(value) {
  if (typeof value !== 'string' || value.length > 64) return false;
  const match = pattern.exec(value);
  return !!match && [match[1], match[2], match[3], match[5]]
    .filter(part => part !== undefined).every(part => Number.isSafeInteger(Number(part)));
}
function channelOf(value) {
  if (!isVersion(value)) throw new Error('Invalid SDK version; expected X.Y.Z, X.Y.Z-preview.N or X.Y.Z-rc.N');
  return pattern.exec(value)[4] || 'stable';
}
module.exports = {isVersion, channelOf};
