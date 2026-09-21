// Diagnostic-only transparent DOM call tracing. No values or page text are logged.
export function installDomCallTrace() {
  const host=window as Window & {__souiTraceDom?:boolean;__souiBenchmarkGalleryCheckpoint?:(name:string,done:boolean)=>boolean};
  const record=(name:string,done:boolean)=>{if(host.__souiTraceDom)host.__souiBenchmarkGalleryCheckpoint?.(name.replace(/[^a-z0-9]/gi,'').slice(0,48),done);};
  function method(prototype:any,key:string) {
    const original=prototype[key];
    if(typeof original!=='function')return;
    prototype[key]=function(...args:any[]) {
      const argument=key==='createTextNode'?'len'+(typeof args[0]==='string'?args[0].length:0)
        :typeof args[0]==='string'?args[0].slice(0,16):args[0]?.nodeName||'';
      const name=key+(this.nodeName||'')+argument;
      record(name,false);const value=original.apply(this,args);record(name,true);return value;
    };
  }
  function property(prototype:any,key:string) {
    const descriptor=Object.getOwnPropertyDescriptor(prototype,key);
    if(!descriptor?.configurable || !descriptor.set)return;
    const original=descriptor.set;
    Object.defineProperty(prototype,key,{...descriptor,set:function(value:any){
      const name='set'+key+(this.nodeName||'')+'len'+(typeof value==='string'?value.length:0);
      record(name,false);original.call(this,value);record(name,true);
    }});
  }
  ['appendChild','insertBefore','removeChild'].forEach(key=>method(Node.prototype,key));
  method(Document.prototype,'createElement');method(Document.prototype,'createTextNode');
  method(Element.prototype,'setAttribute');method(CSSStyleDeclaration.prototype,'setProperty');
  ['textContent','nodeValue'].forEach(key=>property(Node.prototype,key));
  ['innerHTML','className'].forEach(key=>property(Element.prototype,key));
  ['cssText','marginLeft','marginRight','paddingLeft','paddingRight','display','width','height'].forEach(key=>property(CSSStyleDeclaration.prototype,key));
}
