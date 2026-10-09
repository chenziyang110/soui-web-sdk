import fs from 'node:fs';
import path from 'node:path';

const distArgument = process.argv[2] ?? 'dist';
const distRoot = path.resolve(process.cwd(), distArgument);
const manifestPath = path.join(distRoot, '.vite', 'manifest.json');
if (!fs.existsSync(manifestPath)) {
  throw new Error(`Vite manifest is missing: ${manifestPath}`);
}

const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
const application = manifest['index-legacy.html'];
const reactApplication = manifest['react-legacy.html'];
const polyfills = manifest['vite/legacy-polyfills-legacy'];
const styles = manifest['index.html']?.css ?? [];
if (!application?.file || !polyfills?.file) {
  throw new Error('The Vite legacy application and polyfill entries are required for Miniblink.');
}

const localAsset = (value) => {
  if (typeof value !== 'string' || /^(?:[a-z]+:)?\/\//i.test(value) || path.isAbsolute(value)) {
    throw new Error(`Only relative local assets are allowed: ${String(value)}`);
  }
  return `./${value.replaceAll('\\', '/')}`;
};

const polyfillFile = localAsset(polyfills.file);
function resolveLegacyRuntimeEntry(entry) {
  const runtimeImport = (entry?.imports ?? []).find((value) => /main-legacy/.test(value));
  return runtimeImport && manifest[runtimeImport]?.file ? manifest[runtimeImport].file : entry?.file;
}
// The Chrome 49 era DOMException object is exposed but its constructor throws
// "Illegal constructor".  core-js probes that constructor while bootstrapping
// the legacy polyfill bundle, so provide a tiny compatible constructor before
// the generated bundle runs.  Newer hosts keep their native implementation.
const miniblinkPrelude = `    <script>(function(){try{var D=window.DOMException;new D(1,2);}catch(e){var P=D&&D.prototype;var S=function(message,name){this.message=String(message||"");this.name=String(name||"Error");};if(P)S.prototype=P;try{window.DOMException=S;}catch(ignore){}}})();</script>`;
function classicHtml(applicationFile, stylesheetFiles, title, afterImport = '') {
  const stylesheetTags = stylesheetFiles
    .map((file) => `    <link rel="stylesheet" href="${localAsset(file)}">`)
    .join('\n');
  return `<!doctype html>
<html lang="zh-CN">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="color-scheme" content="dark light">
    <title>${title}</title>
${stylesheetTags}
  </head>
  <body>
    <div id="app" aria-live="polite"></div>
${miniblinkPrelude}
    <script src="${polyfillFile}"></script>
    <script>
      System.import('${applicationFile}').then(function (module) {
${afterImport}
      }).catch(function (error) {
        window.__souiBootstrapError = String(error && error.message || error);
        document.getElementById('app').textContent = '页面启动失败：' + window.__souiBootstrapError;
      });
    </script>
  </body>
</html>
`;
}
const html = classicHtml(localAsset(application.file), styles, 'SOUI + Miniblink AI DevKit');
fs.writeFileSync(path.join(distRoot, 'index.html'), html, 'utf8');
if (reactApplication?.file) {
  const reactStyles = manifest['react.html']?.css ?? [];
  // The React HTML entry is a Rollup facade whose execute block only wires
  // imports. Miniblink's SystemJS loader does not reliably run that empty
  // facade, so point the classic document at the concrete legacy runtime
  // entry. That module contains React's self-mount and bridge probe side
  // effect, while preserving its local dependency graph.
  const reactRuntimeEntry = resolveLegacyRuntimeEntry(reactApplication) ?? reactApplication.file;
  fs.writeFileSync(path.join(distRoot, 'react.html'), classicHtml(localAsset(reactRuntimeEntry), reactStyles, 'SOUI + Miniblink React Surface'), 'utf8');
}
console.log(`Miniblink classic entry generated: ${path.join(distRoot, 'index.html')}`);
