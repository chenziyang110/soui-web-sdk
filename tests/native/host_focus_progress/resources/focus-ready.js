(function () {
  'use strict';
  var attempted = false;
  function read(raw) {
    var response = JSON.parse(raw);
    if (!response.ok) throw new Error('Calibration Bridge request failed');
    return response.result;
  }
  function settled(value, done) {
    if (value && typeof value.then === 'function') value.then(done, failed);
    else done(value);
  }
  function failed() { document.getElementById('status').textContent = 'calibration failed'; }
  var timer = setInterval(function () {
    if (attempted || typeof window.__souiNativePostMessage !== 'function') return;
    try {
      var raw = window.__souiNativePostMessage(JSON.stringify({requestId:'focus-bootstrap',
        sessionId:'',origin:'',token:'',method:'bridge.bootstrap',params:{protocol:'soui.bridge/1'}}));
      var packet = JSON.parse(raw);
      if (!packet.ok && packet.error && packet.error.code === 'E_CONTEXT_PENDING') return;
      attempted = true;
      clearInterval(timer);
      var session = read(raw);
      function request(id, method, params) {
        return window.__souiNativePostMessage(JSON.stringify({requestId:id,sessionId:session.sessionId,
          origin:session.origin,token:session.token,method:method,params:params,timeoutMs:5000}));
      }
      settled(request('focus-probe','bridge.probe.report',{ready:true}),function (reply) {
        read(reply);
        settled(request('focus-calibrated','focus.calibration.ready',{ready:true}),function (result) {
          read(result); document.getElementById('status').textContent = 'ready';
        });
      });
    } catch (error) { attempted = true; clearInterval(timer); failed(); }
  },25);
}());
