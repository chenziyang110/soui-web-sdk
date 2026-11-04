"""Prepare an explicit, self-closing Canvas diagnostic page outside the repository."""
import argparse
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent

DRIVER = r'''
    // Explicit lab-only driver. Original scene, draw, simulation and recording bodies stay intact.
    function privateCostDriver() {
      var driver={schema:'soui.private-render-phase-page/v1',phase:'bootstrap',error:null,
        scope:'private-non-input-desktop',warmupRequestedMs:10000,recordRequestedMs:DURATION,
        performanceVerified:false,physicalPresentationMeasured:false,releaseReady:false};
      win.__souiRenderPhaseDiagnostic=driver;
      var setupDue=now()+20000,wholeDue=now()+75000,warmupDue=0,recordSeen=false,timer=0;
      function fail(error) { driver.phase='failed';driver.error=String(error&&error.code||error&&error.message||error); }
      function laterStep() { timer=win.setTimeout(step,100); }
      function persist() {
        var ownReport=JSON.parse(JSON.stringify(lastReport)),saveDue=Math.min(now()+15000,wholeDue);
        ownReport.privateRenderPhaseDiagnostic={schema:driver.schema,scope:driver.scope,
          originalCanvasWorkload:true,warmupRequestedMs:10000,recordRequestedMs:DURATION,
          wholeKernelSnapshotScope:'Whole registered trace lifetime, including setup/warmup/teardown',
          performanceVerified:false,physicalPresentationMeasured:false,releaseReady:false};
        if(utf8Bytes(JSON.stringify(ownReport))>60000) { fail('Diagnostic page report exceeds existing native budget');return; }
        driver.phase='saving';
        bridge.request('benchmark.report',{report:ownReport},credentials,function(error,accepted) {
          if(error||!accepted||accepted.accepted!==true||typeof accepted.id!=='number'||
              Math.floor(accepted.id)!==accepted.id||accepted.id<1||accepted.id>4294967295||typeof accepted.path!=='string') {
            fail(error||'Diagnostic report not accepted');return;
          }
          function poll() {
            if(now()>=saveDue) { fail('Diagnostic report saved-status deadline');return; }
            bridge.request('benchmark.reportStatus',{id:accepted.id},credentials,function(checkError,saved) {
              if(checkError||!saved||saved.path!==accepted.path||now()>=saveDue) { fail(checkError||'Diagnostic saved-status identity/deadline');return; }
              if(saved.state==='pending') { timer=win.setTimeout(poll,100);return; }
              if(saved.state!=='saved') { fail(saved.error||'Diagnostic report writer failed');return; }
              driver.savedReceipt=saved;driver.phase='close-submitted';
              // The copied diagnostic Host alone accepts this owner-queued action.
              // Natural process/Job completion and the final native snapshot are checked externally.
              bridge.request('benchmark.action',{action:'diagnostic-close'},credentials,function(closeError,receipt) {
                if(closeError||!receipt||receipt.accepted!==true)fail(closeError||'Diagnostic close not accepted');
                else driver.phase='close-accepted';
              });
            });
          }
          poll();
        });
      }
      function step() {
        if(now()>=wholeDue) { fail('Diagnostic page absolute deadline');return; }
        if(driver.phase==='bootstrap') {
          if(!connected) {
            if(now()>=setupDue) { fail('Diagnostic page bootstrap deadline');return; }
            laterStep();return;
          }
          if(!identity||identity.backend!=='miniblink'||identity.inputTrace!=='scalar'||
              identity.engineBudgetUs!==0||identity.requestedFrameRate!==60||
              !identity.privateRenderPhase||identity.privateRenderPhase.initialized!==true) {
            fail('Explicit diagnostic MiniBlink/scalar/default configuration missing');return;
          }
          ui.scene.value='canvas';ui.scene.onchange();begin(false);driver.phase='warmup-starting';
        } else if(driver.phase==='warmup-starting') {
          if(running&&scene==='canvas'&&!starting) {
            driver.warmupStarted=now();warmupDue=driver.warmupStarted+10000;driver.phase='warmup';
          } else if(now()>=setupDue) { fail('Diagnostic Canvas warmup did not start');return; }
        } else if(driver.phase==='warmup') {
          if(!running||scene!=='canvas'||recording) { fail('Diagnostic warmup state changed');return; }
          if(now()>=warmupDue) {
            driver.warmupObservedMs=now()-driver.warmupStarted;
            begin(true);driver.phase='recording';
          }
        } else if(driver.phase==='recording') {
          if(recording)recordSeen=true;
          if(recordSeen&&!recording&&!starting&&lastReport&&!nativeBusy()) {
            if(lastReport.scenario!=='canvas'||!lastReport.window||lastReport.window.complete!==true||
                lastReport.window.requestedMs!==DURATION||typeof lastReport.window.observedMs!=='number'||
                !isFinite(lastReport.window.observedMs)||Math.abs(lastReport.window.observedMs-DURATION)>0.000001||
                !lastReport.canvas||lastReport.canvas.drawCalls<1||lastReport.canvas.steps<1||
                !lastReport.nativeMeasurement||lastReport.nativeMeasurement.phase!=='complete'||
                lastReport.nativeMeasurement.measurementUsable!==true||
                !lastReport.nativeMeasurement.begin||lastReport.nativeMeasurement.begin.persisted!==true||
                !lastReport.nativeMeasurement.end||lastReport.nativeMeasurement.end.persisted!==true||
                bridge.pending()!==0||!bridge.nativeOutstandingKnown()) {
              fail('Complete original Canvas/report/drain state missing');return;
            }
            persist();return;
          }
        } else return;
        laterStep();
      }
      win.addEventListener('beforeunload',function(){win.clearTimeout(timer);},false);
      laterStep();
    }
    privateCostDriver();
'''


def digest(data):
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    source = args.source_root.resolve(strict=True)
    output = args.output.resolve()
    repository = HERE.parent.parent.resolve()
    assert args.output.is_absolute() and not output.exists()
    assert not output.is_relative_to(repository) and not output.is_relative_to(source)
    baseline = json.loads((HERE / 'consumer-baseline.json').read_bytes())
    rows = [row for row in baseline['files'] if row['path'].startswith('resources/')]
    assert len(rows) == 3
    originals = []
    prepared = []
    for row in rows:
        path = source / row['path']
        assert path.is_file() and not path.is_symlink()
        raw = path.read_bytes()
        assert digest(raw) == {key: row[key] for key in ['bytes', 'sha256']}
        data = raw
        if path.name == 'benchmark.js':
            newline = b'\r\n' if raw.splitlines(True)[0].endswith(b'\r\n') else b'\n'
            anchor = b'    scheduleFrame(); connect(0);'
            assert raw.count(anchor) == 1
            data = raw.replace(anchor, DRIVER.replace('\n', newline.decode()).encode('ascii') + anchor, 1)
        originals.append((path, raw))
        prepared.append((path.name, data))
    (output / 'page').mkdir(parents=True)
    inventory = []
    for name, data in prepared:
        with (output / 'page' / name).open('xb') as stream:
            stream.write(data)
        inventory.append({'path': name, **digest(data)})
    for path, raw in originals:
        assert path.read_bytes() == raw
    manifest = {'schema': 'soui.private-render-phase-page-preparation/v1', 'preparedOnly': True,
        'source': rows, 'files': inventory, 'driver': digest(DRIVER.encode('ascii')),
        'originalSceneDrawSimulationRecordingBodiesChanged': False, 'newPagePollingMs': 100,
        'warmupRequestedMs': 10000, 'recordRequestedMs': 30000,
        'setupDeadlineMs': 20000, 'wholePageDeadlineMs': 75000, 'saveDeadlineMs': 15000,
        'systemInputGenerated': False, 'guiExecuted': False,
        'performanceVerified': False, 'releaseReady': False}
    with (output / 'page.json').open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(manifest, stream, indent=2)
        stream.write('\n')
    print(json.dumps({'preparedOnly': True, 'page': str(output / 'page'),
        'manifest': digest((output / 'page.json').read_bytes()), 'guiExecuted': False}))


if __name__ == '__main__':
    main()
