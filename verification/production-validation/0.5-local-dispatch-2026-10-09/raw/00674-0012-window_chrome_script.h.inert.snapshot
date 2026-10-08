#pragma once
namespace soui::sdk {
inline constexpr const char* window_chrome_script = R"chrome(
(function(){
  if(window.__souiChromeObserver || !window.__souiNativeWindowChrome) return;
  var pending=false,last='';
  function update(){
    pending=false;
    var nodes=document.querySelectorAll('[data-soui-chrome]'), regions=[];
    for(var i=0;i<nodes.length && regions.length<16;i++){
      var n=nodes[i],r=n.getBoundingClientRect(),s=getComputedStyle(n);
      if(n.disabled || n.getAttribute('aria-disabled')==='true')continue;
      if(r.width<=0 || r.height<=0 || s.visibility==='hidden' || s.display==='none')continue;
      if(r.top<0 || r.bottom>128)continue;
      var hit=document.elementFromPoint((r.left+r.right)/2,(r.top+r.bottom)/2);
      if(hit!==n && !n.contains(hit))continue;
      regions.push([n.getAttribute('data-soui-chrome'),Math.ceil(r.left),Math.ceil(r.top),Math.floor(r.right),Math.floor(r.bottom)]);
    }
    var value=JSON.stringify({width:window.innerWidth,regions:regions});
    if(value!==last && window.__souiNativeWindowChrome(value))last=value;
  }
  function schedule(){if(!pending){pending=true;requestAnimationFrame(update);}}
  window.__souiChromeObserver=new MutationObserver(schedule);
  window.__souiChromeObserver.observe(document.documentElement,{childList:true,subtree:true,attributes:true,attributeFilter:['data-soui-chrome','class','style','hidden','disabled','aria-disabled']});
  window.addEventListener('resize',schedule);
  window.addEventListener('scroll',schedule,true);
  window.addEventListener('load',schedule,true);
  schedule();
})();
)chrome";
}
