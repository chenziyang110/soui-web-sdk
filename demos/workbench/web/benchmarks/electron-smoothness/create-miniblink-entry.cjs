const fs = require('node:fs');
const path = require('node:path');

const distRoot = path.resolve(__dirname, 'dist-legacy');
const manifestPath = path.join(distRoot, '.vite', 'manifest.json');
if (!fs.existsSync(manifestPath)) throw new Error(`Vite manifest is missing: ${manifestPath}`);
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
const app = manifest['benchmark.html'];
const legacyApp = manifest['benchmark-legacy.html'];
const polyfillKey = Object.keys(manifest).find((key) => key.endsWith('vite/legacy-polyfills-legacy'));
const polyfills = polyfillKey ? manifest[polyfillKey] : undefined;
if (!app || !polyfills?.file) throw new Error('The benchmark and legacy polyfill entries are required.');

const localAsset = (value) => {
  if (typeof value !== 'string' || /^(?:[a-z]+:)?\/\//i.test(value) || path.isAbsolute(value)) throw new Error(`Only relative local assets are allowed: ${String(value)}`);
  return `./${value.replaceAll('\\', '/')}`;
};

function resolveRuntimeEntry(entry) {
  if (!entry) return app.file;
  const imports = entry.imports || [];
  const preferred = imports.find((key) => /benchmark-app|benchmark.*legacy|main-legacy/.test(key) && manifest[key]?.file);
  return preferred ? manifest[preferred].file : entry.file;
}

const runtime = resolveRuntimeEntry(legacyApp || app);
const styles = app.css || [];
const html = `<!doctype html>
<html lang="zh-CN">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="color-scheme" content="light">
    <title>SOUI / Electron Smoothness Benchmark</title>
${styles.map((file) => `    <link rel="stylesheet" href="${localAsset(file)}">`).join('\n')}
  </head>
  <body>
    <div id="benchmark-root"></div>
    <script>(function(){try{var D=window.DOMException;new D(1,2);}catch(e){var P=D&&D.prototype;var S=function(message,name){this.message=String(message||"");this.name=String(name||"Error");};if(P)S.prototype=P;try{window.DOMException=S;}catch(ignore){}}})();</script>
    <script src="${localAsset(polyfills.file)}"></script>
    <script>
      System.import('${localAsset(runtime)}').then(function () {}).catch(function (error) {
        window.__souiBootstrapError = String(error && error.message || error);
        document.getElementById('benchmark-root').textContent = 'Benchmark 启动失败：' + window.__souiBootstrapError;
      });
    </script>
  </body>
</html>
`;
fs.writeFileSync(path.join(distRoot, 'benchmark.html'), html, 'utf8');
// The existing SOUI host validates the resource root through index.html even
// when --entry selects another local page. Keep this alias byte-identical so
// validation does not change which benchmark document is loaded.
fs.writeFileSync(path.join(distRoot, 'index.html'), html, 'utf8');
// Probe-only entry: write one S01 result and let the native host exit. It is
// deliberately separate from benchmark.html and never used for comparison.
const smoke = html.replace('</body>', `    <script>(function(){
      var timer = setInterval(function(){
        if(window.__souiBenchmarkReady && window.__souiBenchmark){
          clearInterval(timer);
          setTimeout(function(){window.__souiBenchmark.finish();},500);
        }
      },50);
      setTimeout(function(){clearInterval(timer);},15000);
    })();</script>\n  </body>`);
fs.writeFileSync(path.join(distRoot, 'benchmark-smoke.html'), smoke, 'utf8');
console.log(`Miniblink benchmark entry generated: ${path.join(distRoot, 'benchmark.html')}`);
