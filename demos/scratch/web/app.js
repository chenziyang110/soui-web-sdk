import {connectSouiNative} from './soui/index.js';
import {lessonProject} from './lessons.js';
import {buildPerformanceProject, performanceScenes} from './performance-scenes.js';
import {createPerformanceMeter} from './performance-meter.js';

const GUI = window.GUI;
const byId = id => document.getElementById(id);
let vm, client, baseProject, ready = false, busy = false, messageTimer;
let sdkInfo, currentPerformance = null;
const maximumProject = 16 * 1024 * 1024;
const status = text => {byId('native-status').textContent = text;};
function message(text) {
  clearTimeout(messageTimer); byId('message').textContent = text; byId('message').hidden = false;
  messageTimer = setTimeout(() => {byId('message').hidden = true;}, 6000);
}
function failed(error) {
  console.error(error); const text = String(error?.message || error);
  if (!ready) {byId('loading').innerHTML = ''; const heading = document.createElement('h1'); heading.textContent = '暂时无法打开创作室'; const detail = document.createElement('p'); detail.textContent = text; byId('loading').append(heading, detail);}
  else message(text);
  status('遇到问题 · 请查看提示');
}
async function nativeReady() {
  client = await connectSouiNative({requestTimeoutMs:60000});
  const info = await client.call('app.info', {});
  sdkInfo = info;
  if (!/^0\.6\.0(?:-|$)/.test(info.sdkVersion)) throw new Error('此演示需要 SOUI SDK 0.6.0 系列。');
  status(`SOUI ${info.sdkVersion} · ${info.backend} · 本地素材`);
  byId('save').disabled = false;
  return info;
}
const connection = nativeReady();
connection.catch(failed);
function confirmReplace(text) {
  const dialog = byId('confirm-dialog'); byId('confirm-text').textContent=text;
  if(dialog.open) return Promise.resolve(false);
  return new Promise(resolve => {dialog.onclose=()=>resolve(dialog.returnValue==='continue');dialog.showModal();});
}
try {
  if (!GUI) throw new Error('Scratch 编辑器未能加载，请重新构建本地资源。');
  const storage = new GUI.ScratchStorage();
  const defaults = GUI.buildDefaultProject();
  baseProject = JSON.parse(defaults.find(asset => asset.assetType === 'Project').data);
  for (const asset of defaults) storage.builtinHelper._store(storage.AssetType[asset.assetType], storage.DataFormat[asset.dataFormat], asset.assetType === 'Project' ? JSON.stringify(lessonProject(baseProject)) : asset.data, asset.id);
  storage.addWebStore([storage.AssetType.ImageVector, storage.AssetType.ImageBitmap, storage.AssetType.Sound], asset => `./assets/${asset.assetId}.${asset.dataFormat}`);
  const config = {storage:{scratchStorage:storage, getLibraryAssetUrl:(id,format) => `./assets/${id}.${format}`, saveProject:() => Promise.reject(new Error('请使用窗口上方的“保存 .sb3”。'))}};
  GUI.setAppElement(byId('editor'));
  const state = new GUI.EditorState({locale:'zh-cn'}, () => config);
  const editor = GUI.createStandaloneRoot(state, byId('editor'));
  editor.render({basePath:'./', platform:'DESKTOP', canEditTitle:true, canSave:false, canManageFiles:false, canCreateNew:false, canShare:false, canUseCloud:false, enableCommunity:false, backpackVisible:false, projectId:'0', projectTitle:'小猫画圆',
    onVmInit: machine => {vm = machine;},
    onProjectLoaded: () => {ready = true; byId('loading').hidden = true; connection.then(() => client.call('bridge.probe.report', {ready:true})).catch(failed);}
  });
  // Local diagnostics contain application state only; Bridge credentials stay in the public client.
  window.souiScratch = {get vm(){return vm;}, get ready(){return ready;}, state};
  setTimeout(() => {if (!ready) failed(new Error('编辑器加载超时，请确认 WebView2 Runtime 支持 WebGL。'));}, 45000);
} catch (error) {failed(error);}
byId('help').onclick = () => {byId('tips').hidden = !byId('tips').hidden; byId('help').setAttribute('aria-expanded', String(!byId('tips').hidden));};
document.querySelectorAll('[data-lesson]').forEach(button => {button.onclick = async () => {
  if (!ready || busy) return;
  if (!await confirmReplace('切换示例会替换当前作品。需要保留时，请先保存 .sb3。继续吗？')) return;
  try {busy = true; performanceMeter.stop(); currentPerformance=null; vm.stopAll(); vm.setCompatibilityMode(true); await vm.loadProject(JSON.stringify(lessonProject(baseProject, button.dataset.lesson))); message('示例已准备好，点击绿色旗帜开始！');} catch(error){failed(error);} finally {busy = false;}
};});
byId('open').onclick = () => {if(ready && !busy) byId('project-file').click();};
byId('project-file').onchange = async event => {
  const file = event.target.files[0]; event.target.value = '';
  if (!file) return;
  if (file.size > maximumProject || !/\.sb3$/i.test(file.name)) return message('请选择不超过 16 MB 的 .sb3 作品。');
  if (!await confirmReplace('打开作品会替换当前内容。请确认已保存需要保留的作品。')) return;
  try {busy=true; performanceMeter.stop(); currentPerformance=null; vm.stopAll(); vm.setCompatibilityMode(true); await vm.loadProject(await file.arrayBuffer()); message('作品已打开：'+file.name);} catch(error){failed(error);} finally{busy=false;}
};
byId('save').onclick = async () => {
  if (!ready || busy || !client) return;
  try {
    busy = true; byId('save').disabled = true;
    const blob = await vm.saveProjectSb3();
    if (blob.size > maximumProject) throw new Error('此演示支持保存不超过 16 MB 的作品。');
    const data = new Uint8Array(await blob.arrayBuffer());
    let result;
    // Base64 plus fields must fit the Bridge's 32 KiB params limit, not only its envelope limit.
    for (let offset=0; offset<data.length; offset+=16*1024) {
      const chunk = data.subarray(offset, offset+16*1024);
      let binary = ''; for (const byte of chunk) binary += String.fromCharCode(byte);
      result = await client.call('app.saveProject', {base64:btoa(binary),offset,final:offset+chunk.length===data.length});
      if(result.error) throw new Error('保存作品失败：'+result.error);
    }
    message(result?.saved ? '作品已保存，下次可以用“打开作品”继续创作。' : '已取消保存。');
  } catch(error) {failed(error);} finally {busy = false; byId('save').disabled = !client;}
};

function updatePerformance(snapshot) {
  byId('perf-fps').textContent=snapshot.rafFps==null?'—':snapshot.rafFps.toFixed(1);
  byId('perf-p95').textContent=snapshot.frameP95Ms==null?'—':snapshot.frameP95Ms.toFixed(1)+' ms';
  byId('perf-p95').title=snapshot.sampleCoverage==='rolling-window'?'最近 2000 个 rAF 间隔的 P95':'本轮全部 rAF 间隔的 P95';
  byId('perf-long').textContent=snapshot.longFrames??'—';
  byId('perf-targets').textContent=snapshot.targets==null?'—':`${snapshot.sprites} / ${snapshot.clones}`;
  byId('perf-threads').textContent=snapshot.threads??'—';
  byId('perf-heap').textContent=snapshot.heapBytes==null?'不支持':(snapshot.heapBytes/1048576).toFixed(1)+' MB';
  if(snapshot.recording) byId('performance-state').textContent=`正在记录：${(snapshot.elapsedMs/1000).toFixed(0)} / 30 秒；保持窗口可见。`;
  else if(snapshot.status==='invalid') byId('performance-state').textContent='记录无效：窗口被隐藏或最小化，请重新记录。';
  else if(snapshot.completed) byId('performance-state').textContent=snapshot.valid?'记录完成，可导出 JSON 进行版本对比。':'记录无效，请重新记录。';
}
const performanceMeter=createPerformanceMeter({getVM:()=>vm,onUpdate:updatePerformance});
function describePerformance() {
  const scene=performanceScenes.find(item=>item.id===byId('performance-scene').value);
  const level=scene.levels[byId('performance-level').value];
  byId('performance-description').textContent=`${scene.description} 当前配置：${level.sprites} 个角色、${level.clones} 个克隆体。`;
}
describePerformance();
byId('performance-scene').onchange=describePerformance;
byId('performance-level').onchange=describePerformance;
byId('performance-toggle').onclick=()=>{byId('performance-panel').hidden=!byId('performance-panel').hidden;byId('performance-toggle').setAttribute('aria-expanded',String(!byId('performance-panel').hidden));};
byId('performance-close').onclick=()=>{byId('performance-panel').hidden=true;byId('performance-toggle').setAttribute('aria-expanded','false');};
byId('performance-load').onclick=async()=>{
  if(!ready||busy)return;
  if(!await confirmReplace('载入性能场景会替换当前作品。需要保留时请先保存 .sb3。'))return;
  try {
    busy=true;performanceMeter.reset();vm.stopAll();
    const sceneId=byId('performance-scene').value, level=byId('performance-level').value;
    const scene=performanceScenes.find(item=>item.id===sceneId);
    const project=buildPerformanceProject(baseProject,sceneId,level);
    const digest=await crypto.subtle.digest('SHA-256',new TextEncoder().encode(JSON.stringify(project)));
    const workloadSha256=Array.from(new Uint8Array(digest),byte=>byte.toString(16).padStart(2,'0')).join('');
    await vm.loadProject(JSON.stringify(project));
    vm.setCompatibilityMode(false);vm.greenFlag();
    currentPerformance={sceneId,level,metadata:{sdkVersion:sdkInfo?.sdkVersion??null,buildId:sdkInfo?.buildId??null,backend:sdkInfo?.backend??null,runtimeVersion:sdkInfo?.runtimeVersion??null,workloadRecipe:project.meta.agent,workloadSha256,sceneSeed:scene.seed,configuredLoad:scene.levels[level],vmTargetStepHz:60,runtimeStepMs:vm.runtime.currentStepTime??null}};
    byId('performance-state').textContent=`${scene.title}已运行。等待克隆/角色数量稳定后，点击“记录 30 秒”。`;
    message('性能场景正在运行；可在负载中拖动积木、切换造型或操作舞台。');
  }catch(error){failed(error);}finally{busy=false;}
};
byId('performance-stop').onclick=()=>{vm?.stopAll();performanceMeter.stop();byId('performance-state').textContent='已停止全部脚本。';};
byId('performance-measure').onclick=()=>{
  if(!ready||busy||!currentPerformance)return message('请先载入并运行一个性能场景。');
  performanceMeter.start({...currentPerformance,durationMs:30000});
};
byId('performance-export').onclick=async()=>{
  if(!client||busy)return;
  const report=performanceMeter.collectReport();
  if(!report.metrics?.intervalsObserved && !report.intervalsObserved)return message('请先记录一轮性能数据。');
  if(performanceMeter.snapshot().recording)return message('请等待记录完成，或先停止记录。');
  try {busy=true;const result=await client.call('app.saveMetrics',{report:JSON.stringify(report)});if(result.error)throw new Error(result.error);message(result.saved?'性能报告已保存。':'已取消保存报告。');}catch(error){failed(error);}finally{busy=false;}
};
window.addEventListener('pagehide',()=>performanceMeter.dispose(),{once:true});
window.souiScratchPerformance={meter:performanceMeter,get configuration(){return currentPerformance;}};
