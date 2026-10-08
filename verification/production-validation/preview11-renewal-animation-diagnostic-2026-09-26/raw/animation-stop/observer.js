(function(){
  var messages=[],start=new Date().getTime();
  function safe(value){return String(value).replace(/[\r\n]/g," ").slice(0,220);}
  function show(){var box=document.getElementById("soui-diagnostic-stages");if(box)box.textContent=messages.join("\n");}
  window.__souiStage=function(label){if(messages.length<40)messages.push((new Date().getTime()-start)+"ms "+safe(label));window.__souiStageText=messages.join("\n");show();};
  window.__souiStage("head-observer");
  window.addEventListener("error",function(e){window.__souiStage("error: "+safe(e.message||"resource-error")+" file="+safe(e.filename||e.target&&e.target.src||""));},true);
  document.addEventListener("DOMContentLoaded",function(){window.__souiStage("dom-content-loaded");});
  window.addEventListener("load",function(){window.__souiStage("window-load System="+typeof window.System+" native="+typeof window.__souiNativePostMessage);});
  function inspect(){
    var scripts=document.getElementsByTagName("script"),details=document.getElementById("details");
    window.__souiStage("parser state="+document.readyState+" scripts="+scripts.length+" details="+(details?"present":"absent"));
    window.__souiStage("globals System="+typeof window.System+" native="+typeof window.__souiNativePostMessage+" exception="+typeof window.DOMException);
    for(var i=0;i<scripts.length;i++){var s=scripts[i];window.__souiStage("script "+i+" type="+(s.type||"classic")+" src="+(s.src?s.src.split("/").pop():"inline")+" text="+(s.text||"").length);}
  }
  setTimeout(inspect,1000);
  setTimeout(inspect,20000);
  setTimeout(function(){window.__souiStage("animation-stop-before state="+document.readyState);var spinner=document.querySelector(".spinner");if(spinner)spinner.style.animation="none";window.__souiStage("animation-stop-after state="+document.readyState);},15000);
})();