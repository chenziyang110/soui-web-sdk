// Browser functional check; native Bridge and save dialog require desktop verification.
const {chromium} = require('playwright');
const http = require('node:http');
const fs = require('node:fs/promises');
const path = require('node:path');
const root = path.join(__dirname, 'web/dist');
const output = path.resolve(__dirname, '../../out/scratch-verification');
const mime = {'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.json':'application/json','.wasm':'application/wasm'};
(async () => {
  await fs.mkdir(output, {recursive:true});
  const server = http.createServer(async (request,response) => {
    const target = path.resolve(root, '.' + new URL(request.url, 'http://local').pathname);
    if(!target.startsWith(root+path.sep)) {response.writeHead(403).end();return;}
    try {const data=await fs.readFile(target);response.setHeader('Content-Type',mime[path.extname(target)]||'application/octet-stream');response.end(data);} catch {response.writeHead(404).end();}
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  let browser;
  const report = {scope:'Headless editor/VM/assets only; not native host verification', errors:[],missing:[]};
  try {
    browser=await chromium.launch({headless:true});
    const page=await browser.newPage({viewport:{width:1440,height:1000}});
    page.on('pageerror', error => report.errors.push(error.message));
    page.on('response', response => {if(response.status()>=400)report.missing.push(response.url());});
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
    await page.waitForFunction(() => window.souiScratch?.ready, {timeout:45000});
    report.before=await page.evaluate(() => {const s=window.souiScratch.vm.runtime.targets.find(t=>!t.isStage);return {x:s.x,y:s.y,direction:s.direction,blocks:Object.keys(s.blocks._blocks).length};});
    await page.evaluate(()=>window.souiScratch.vm.greenFlag());
    await page.waitForFunction(before=>{const s=window.souiScratch.vm.runtime.targets.find(t=>!t.isStage);return s.x!==before.x || s.y!==before.y || s.direction!==before.direction;}, report.before);
    report.after=await page.evaluate(()=>{const s=window.souiScratch.vm.runtime.targets.find(t=>!t.isStage);return {x:s.x,y:s.y,direction:s.direction};});
    await page.waitForFunction(()=>!window.souiScratch.vm.runtime.threads.some(t=>t.status===0),{timeout:10000});
    report.projectBytes=await page.evaluate(async()=> {const vm=window.souiScratch.vm;const project=await vm.saveProjectSb3();await vm.loadProject(await project.arrayBuffer());return project.size;});
    // Verify SVG editing's opaque Paper sandbox really starts, rather than merely displaying a static stage.
    await page.getByText('造型', {exact:true}).first().click();
    await page.waitForFunction(()=>document.querySelector('iframe[sandbox="allow-scripts"]'));
    await page.getByRole('button',{name:'② 按键漫游',exact:true}).click();
    await page.locator('#confirm-dialog button.continue').click();
    await page.waitForFunction(()=>Object.values(window.souiScratch.vm.editingTarget.blocks._blocks).some(b=>b.opcode==='event_whenkeypressed'));
    await page.getByText('代码',{exact:true}).first().click();
    await page.locator('canvas').first().click();
    await page.keyboard.press('ArrowRight');
    await page.waitForFunction(()=>window.souiScratch.vm.editingTarget.x===15);
    report.keyboardX=await page.evaluate(()=>window.souiScratch.vm.editingTarget.x);
    await page.screenshot({path:path.join(output,'editor.png')});
    report.pass=report.before.blocks>=6 && (report.after.x!==report.before.x || report.after.y!==report.before.y || report.after.direction!==report.before.direction) && report.projectBytes>1000 && report.errors.length===0 && report.missing.length===0;
    await fs.writeFile(path.join(output,'web-report.json'),JSON.stringify(report,null,2));
    console.log(JSON.stringify(report));
    if(!report.pass) process.exitCode=1;
  } catch(error) {report.failure=String(error);await fs.writeFile(path.join(output,'web-report.json'),JSON.stringify(report,null,2));console.error(JSON.stringify(report));process.exitCode=1;}
  finally {if(browser)await browser.close();server.close();}
})();
