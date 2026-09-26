(function(){
  var messages=[],start=new Date().getTime();
  function safe(value){return String(value).replace(/[\r\n]/g," ").slice(0,220);}
  function show(){var box=document.getElementById("soui-diagnostic-stages");if(box)box.textContent=messages.join("\n");}
  window.__souiStage=function(label){if(messages.length<40)messages.push((new Date().getTime()-start)+"ms "+safe(label));window.__souiStageText=messages.join("\n");show();};
  window.__souiStage("head-observer");
  window.addEventListener("error",function(e){window.__souiStage("error: "+safe(e.message||"resource-error")+" file="+safe(e.filename||e.target&&e.target.src||""));},true);
  document.addEventListener("DOMContentLoaded",function(){window.__souiStage("dom-content-loaded");});
  window.addEventListener("load",function(){window.__souiStage("window-load System="+typeof window.System+" native="+typeof window.__souiNativePostMessage);});
  setTimeout(function(){window.__souiStage("timer-1000 state="+document.readyState);},1000);
})();