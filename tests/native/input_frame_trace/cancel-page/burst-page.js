(function () {
  'use strict';
  // Correctness workload only. No timing/FPS claims; finite useful computation,
  // not a timer busy-wait, provides a real long JS callback for overload.
  var state={down:0,up:0,click:0,keyDown:0,keyUp:0,compositionStart:0,compositionEnd:0,
    compositionUpdates:0,inputEvents:0,checksum:0,loads:0,eventOverflow:false};
  var events=[],subject=document.getElementById('subject'),ime=document.getElementById('ime');
  function publish(){document.getElementById('state').textContent=JSON.stringify(snapshot(),null,2);}
  function log(name,data){if(events.length<2048)events.push({name:name,data:String(data||'').slice(0,64)});else state.eventOverflow=true;}
  function snapshot(){return {schema:'soui.input-cancel-oracle/v1',run_id:window.souiBurstConfig&&window.souiBurstConfig.run,
    counters:state,value:ime.value,events:events.slice()};}
  function scheduleLoad(){
    var armed=document.getElementById('load');if(!armed.checked)return;armed.checked=false;
    setTimeout(function(){var checksum=1;for(var n=0;n<200000000;++n)checksum=(checksum*1664525+n+1013904223)>>>0;
      state.checksum=checksum;++state.loads;log('bounded-computation-complete',checksum);publish();},250);
  }
  subject.addEventListener('mousedown',function(){++state.down;log('down');publish();scheduleLoad();},false);
  subject.addEventListener('mouseup',function(){++state.up;log('up');publish();},false);
  subject.addEventListener('click',function(){++state.click;log('click');publish();},false);
  ime.addEventListener('keydown',function(event){++state.keyDown;log('keyDown',event.keyCode);},false);
  ime.addEventListener('keyup',function(event){++state.keyUp;log('keyUp',event.keyCode);publish();},false);
  ime.addEventListener('compositionstart',function(){++state.compositionStart;log('compositionStart');publish();},false);
  ime.addEventListener('compositionupdate',function(event){++state.compositionUpdates;log('compositionUpdate',event.data);publish();scheduleLoad();},false);
  ime.addEventListener('compositionend',function(event){++state.compositionEnd;log('compositionEnd',event.data);publish();},false);
  ime.addEventListener('input',function(){++state.inputEvents;log('input',ime.value);publish();},false);
  document.getElementById('snapshot').addEventListener('click',publish,false);
  window.inputCancelOracle={snapshot:snapshot};publish();
}());
