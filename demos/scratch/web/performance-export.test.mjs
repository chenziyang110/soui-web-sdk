import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {createPerformanceMeter} from './performance-meter.js';

// Execute the real application and onclick handler. Import dependencies and
// browser/native boundaries are supplied by the fixture; export decisions and
// serialization stay production code. No real GUI or measured run is created.
async function application() {
  let now=0,nextId=0;
  const nodes=new Map(),timers=new Map(),frames=new Map(),documentListeners=new Map(),saved=[];
  const node=id=>{
    if(!nodes.has(id))nodes.set(id,{hidden:false,value:id==='performance-level'?'heavy':'sprites',textContent:'',options:[],
      setAttribute(){},append(...values){this.options.push(...values);},showModal(){this.open=true;}});
    return nodes.get(id);
  };
  const document={visibilityState:'visible',getElementById:node,querySelectorAll:()=>[],
    createElement:()=>({}),addEventListener:(event,fn)=>documentListeners.set(event,fn),removeEventListener:event=>documentListeners.delete(event)};
  const environment={document,navigator:{userAgent:'export-contract-fixture'},performance:{now:()=>now},
    innerWidth:1424,innerHeight:861,devicePixelRatio:1,
    requestAnimationFrame:fn=>{const id=++nextId;frames.set(id,fn);return id;},cancelAnimationFrame:id=>frames.delete(id),
    setTimeout:(fn,delay)=>{const id=++nextId;timers.set(id,{fn,at:now+delay});return id;},clearTimeout:id=>timers.delete(id),
    addEventListener(){},removeEventListener(){}};
  const runtime={targets:[{isStage:true,isOriginal:true}],threads:[]};
  const client={call:async(method,params)=>{
    if(method==='app.info')return {sdkVersion:'0.6.0',backend:'fixture'};
    if(method==='app.saveMetrics'){saved.push(JSON.parse(params.report));return {saved:true};}
    return {ready:true};
  }};
  const GUI={ScratchStorage:class {constructor(){this.builtinHelper={_store(){}};this.AssetType={Project:'Project',ImageVector:'svg',ImageBitmap:'png',Sound:'sound'};this.DataFormat={};}addWebStore(){}},
    buildDefaultProject:()=>[{assetType:'Project',data:'{"targets":[]}'}],setAppElement(){},EditorState:class {},
    createStandaloneRoot:()=>({render(options){options.onVmInit({runtime});options.onProjectLoaded();}})};
  const window={GUI,addEventListener(){}};
  const source=readFileSync(process.env.SOUI_SCRATCH_EXPORT_APP_SOURCE??new URL('./app.js',import.meta.url),'utf8');
  const imports=source.match(/^import .*;\r?$/gm)??[];
  assert.equal(imports.length,4,'fixture must explicitly supply each application import');
  vm.runInNewContext(source.replace(/^import .*;\r?$/gm,''),{window,document,console,
    setTimeout:environment.setTimeout,clearTimeout:environment.clearTimeout,
    connectSouiNative:async()=>client,lessonProject:project=>project,
    performanceScenes:[{id:'sprites',title:'fixture',levels:{heavy:{label:'heavy'}}}],buildPerformanceProject(){throw Error('not used');},
    createPerformanceMeter:options=>createPerformanceMeter({...options,environment})},{filename:'app.js'});
  // Native startup resolves asynchronously before any export action is allowed.
  await new Promise(resolve=>setImmediate(resolve));
  const meter=window.souiScratchPerformance.meter;
  return {meter,saved,node,
    async export(){await node('performance-export').onclick();},
    deadline(){now=1000;for(const [id,timer]of [...timers])if(timer.at<=now){timers.delete(id);timer.fn();}},
    frame(){now=10;const work=[...frames.values()];frames.clear();for(const fn of work)fn(now);},
    hidden(){document.visibilityState='hidden';documentListeners.get('visibilitychange')?.();}};
}

test('real UI exports a completed zero-frame or one-frame window without losing poor results',async()=>{
  for(const oneFrame of [false,true]){
    const app=await application();app.meter.start({durationMs:1000});if(oneFrame)app.frame();app.deadline();
    const before=app.meter.collectReport();assert.equal(before.intervalsObserved,0);assert.equal(before.validForWindowRateComparison,true);
    await app.export();assert.equal(app.saved.length,1);assert.deepEqual(app.saved[0],before);
    assert.equal(app.saved[0].windowRafFps,oneFrame?1:0);assert.equal(app.saved[0].censored,true);
  }
});
test('real UI preserves stopped and invalid evidence even before the first frame',async()=>{
  for(const terminal of ['stopped','invalid']){
    const app=await application();app.meter.start({durationMs:1000});
    if(terminal==='invalid')app.hidden();else app.meter.stop();
    const before=app.meter.collectReport();assert.equal(before.status,terminal);assert.equal(before.intervalsObserved,0);
    await app.export();assert.equal(app.saved.length,1);assert.deepEqual(app.saved[0],before);assert.equal(app.saved[0].validForComparison,false);
  }
});
test('real UI rejects idle/reset state and waits for an active recording',async()=>{
  const app=await application();await app.export();assert.equal(app.saved.length,0);assert.match(app.node('message').textContent,/请先记录/);
  app.meter.start({durationMs:1000});await app.export();assert.equal(app.saved.length,0);assert.match(app.node('message').textContent,/请等待/);
  app.meter.stop();app.meter.reset();await app.export();assert.equal(app.saved.length,0);assert.match(app.node('message').textContent,/请先记录/);
});
