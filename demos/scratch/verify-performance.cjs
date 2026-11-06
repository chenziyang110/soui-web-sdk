// Functional load check, not a performance comparison or native presentation benchmark.
const {chromium}=require('playwright');
const http=require('node:http'),fs=require('node:fs/promises'),path=require('node:path');
const root=path.join(__dirname,'web/dist');
const output=path.resolve(__dirname,'../../out/scratch-verification');
const mime={'.html':'text/html;charset=utf-8','.js':'text/javascript;charset=utf-8','.css':'text/css','.svg':'image/svg+xml','.json':'application/json','.wasm':'application/wasm'};
(async()=>{
  await fs.mkdir(output,{recursive:true});
  const server=http.createServer(async(req,res)=>{
    const target=path.resolve(root,'.'+new URL(req.url,'http://local').pathname);
    if(!target.startsWith(root+path.sep)){res.writeHead(403).end();return;}
    try{res.setHeader('Content-Type',mime[path.extname(target)]||'application/octet-stream');res.end(await fs.readFile(target));}catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  const report={scope:'Headless functional validation of load scenes and measurement lifecycle; values are not SDK performance claims',errors:[],missing:[],scenes:[]};
  try{
    browser=await chromium.launch({headless:true});
    const page=await browser.newPage({viewport:{width:1440,height:1000}});
    page.on('pageerror',error=>report.errors.push(error.message));
    page.on('response',response=>{if(response.status()>=400)report.missing.push(response.url());});
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
    await page.waitForFunction(()=>window.souiScratch?.ready);
    await page.locator('#performance-toggle').click();
    await page.locator('#performance-level').selectOption('heavy');
    for(const [sceneId,expected] of [['sprites',241],['particles',282],['scripts',121]]){
      await page.locator('#performance-scene').selectOption(sceneId);
      await page.locator('#performance-load').click();
      await page.locator('#confirm-dialog .continue').click();
      await page.waitForFunction(id=>window.souiScratchPerformance.configuration?.sceneId===id,sceneId,{timeout:30000});
      await page.waitForFunction(count=>window.souiScratch.vm.runtime.targets.length===count,expected,{timeout:20000});
      const before=await page.evaluate(()=>{const s=window.souiScratch.vm.runtime.targets.find(t=>!t.isStage&&t.visible);return {x:s.x,y:s.y};});
      await page.waitForFunction(previous=>{const s=window.souiScratch.vm.runtime.targets.find(t=>!t.isStage&&t.visible);return s.x!==previous.x||s.y!==previous.y;},before);
      report.scenes.push(await page.evaluate(()=>({sceneId:window.souiScratchPerformance.configuration.sceneId,targets:window.souiScratch.vm.runtime.targets.length,threads:window.souiScratch.vm.runtime.threads.length})));
      if(sceneId==='sprites'){
        await page.locator('#performance-measure').click();
        await page.waitForFunction(()=>window.souiScratchPerformance.meter.snapshot().completed,null,{timeout:45000});
        const measured=await page.evaluate(()=>window.souiScratchPerformance.meter.collectReport());
        if(!measured.validForComparison||measured.intervalsObserved<2||Buffer.byteLength(JSON.stringify(measured))>24576)throw Error('Measurement report contract failed');
        await fs.writeFile(path.join(output,'performance-report-sample.json'),JSON.stringify({...measured,testScope:'Headless functional sample; no native performance comparison'},null,2));
      }
      await page.screenshot({path:path.join(output,`performance-${sceneId}.png`)});
      await page.locator('#performance-stop').click();
      await page.waitForFunction(()=>window.souiScratch.vm.runtime.threads.length===0);
    }
    report.pass=report.errors.length===0&&report.missing.length===0;
    if(!report.pass)process.exitCode=1;
  }catch(error){report.failure=String(error);report.pass=false;process.exitCode=1;}
  finally{await fs.writeFile(path.join(output,'performance-functional-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));if(browser)await browser.close();server.close();}
})();
