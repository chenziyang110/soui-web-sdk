// Classic local dedicated Worker. No MessagePort transfer: vendor port-post asserts main thread.
var documentTag = null;
var receiveOrdinal = 0;
var armedTimer = null;
var pulseOrdinal = 0;
self.onmessage = function (event) {
  var value = event.data;
  if (!value || typeof value !== 'object' || typeof value.tag !== 'string') throw new Error('Invalid worker request');
  if (value.kind === 'initialize') {
    if (documentTag !== null || Object.keys(value).length !== 2) throw new Error('Worker initialized twice');
    documentTag = value.tag;
    self.postMessage({tag:documentTag,kind:'ready'});
    return;
  }
  if (value.tag !== documentTag) throw new Error('Worker identity mismatch');
  if (value.kind === 'echo') {
    if (Object.keys(value).length !== 3 || value.sequence !== receiveOrdinal) throw new Error('Worker FIFO mismatch');
    self.postMessage({tag:documentTag,kind:'echo',sequence:value.sequence,receiveOrdinal:receiveOrdinal++});
  } else if (value.kind === 'close') {
    if (Object.keys(value).length !== 2 || armedTimer === null) throw new Error('Invalid worker close');
    self.postMessage({tag:documentTag,kind:'closing'});
    // Leave the continuing emitter armed: a no-op close must fail the page's quiet interval.
    self.close();
  } else if (value.kind === 'arm') {
    if (Object.keys(value).length !== 2 || armedTimer !== null) throw new Error('Invalid worker arm');
    self.postMessage({tag:documentTag,kind:'armed'});
    // The page first observes a real pulse, then requests close/terminate without cancelling this timer.
    armedTimer = setInterval(function () {
      self.postMessage({tag:documentTag,kind:'pulse',sequence:pulseOrdinal++});
    },50);
  } else throw new Error('Unknown worker request');
};
