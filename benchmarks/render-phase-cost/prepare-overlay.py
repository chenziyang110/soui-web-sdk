"""Materialize an explicit diagnostic overlay; never edit default vendor files."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPOSITORY = HERE.parent.parent

def digest(data):
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

def replace_once(text, before, after):
    # Match normalized ASCII anchors, but keep every equal source line's actual
    # bytes, including mixed LF/CRLF. Only inserted/replaced lines choose the
    # nearby line ending. No whole-file EOL normalization is permitted.
    positions, normalized, cursor = [], [], 0
    while cursor < len(text):
        positions.append(cursor)
        if text[cursor:cursor+2] == '\r\n':
            normalized.append('\n'); cursor += 2
        else:
            normalized.append(text[cursor]); cursor += 1
    positions.append(len(text)); normalized=''.join(normalized)
    assert normalized.count(before) == 1, 'Source anchor is missing or ambiguous: ' + before[:90]
    begin=normalized.index(before); end=begin+len(before)
    original=text[positions[begin]:positions[end]]
    old_lines=before.splitlines(True); raw_lines=original.splitlines(True)
    new_lines=after.splitlines(True)
    assert len(old_lines)==len(raw_lines)
    nearby=next(('\r\n' if line.endswith('\r\n') else '\n'
        for line in raw_lines if line.endswith('\n')), '\n')
    replacement=[]
    for operation,i,j,k,l in difflib.SequenceMatcher(a=old_lines,b=new_lines,autojunk=False).get_opcodes():
        if operation=='equal': replacement.extend(raw_lines[i:j])
        elif operation in ('replace','insert'):
            replacement.extend(line[:-1]+nearby if line.endswith('\n') else line for line in new_lines[k:l])
    return text[:positions[begin]]+''.join(replacement)+text[positions[end]:]

def transform(name, raw):
    # latin1 is a reversible byte view. Preserve upstream legacy comment bytes
    # and CRLF rather than decoding/re-encoding them as UTF8.
    text = raw.decode('latin1')
    lines = text.splitlines(True)
    first = next(i for i, line in enumerate(lines) if line.startswith('#include '))
    newline='\r\n' if lines[first].endswith('\r\n') else '\n'
    lines.insert(first + 1, '#include "RenderPhaseCostHooks.h"'+newline)
    text = ''.join(lines)
    if name == 'content/common/InputFrameTrace.cpp':
        text = replace_once(text, 'namespace content { namespace input_frame_trace {',
            '#include "RenderPhaseCostHooks.inc"\n\nnamespace content { namespace input_frame_trace {')
        text = replace_once(text,
            '  storage_->session = session; storage_->recording_mode = requested; storage_->configured = true;\n  return true;',
            '  storage_->session = session; storage_->recording_mode = requested; storage_->configured = true;\n'
            '  if (this == &process_recorder()) phase_cost::bind_trace(session.process_id,\n'
            '      session.process_incarnation, session.run_id_high, session.run_id_low, session.qpc_frequency);\n'
            '  return true;')
        text = replace_once(text, 'void recorder::stop() noexcept {\n',
            'void recorder::stop() noexcept {\n  if (this == &process_recorder()) phase_cost::stop();\n')
        text = replace_once(text,
            '~local_registration() { local_writer = nullptr; context_valid = false; }',
            '~local_registration() { phase_cost::detach(); local_writer = nullptr; context_valid = false; }')
        text = replace_once(text,
            '    state.registrars.fetch_sub(1,std::memory_order_release);\n    if(resumed)local_writer=&registration.producer;',
            '    if(resumed) {\n      local_writer=&registration.producer;\n'
            '      phase_cost::attach(registration.producer.slot_, GetCurrentThreadId());\n    }\n'
            '    state.registrars.fetch_sub(1,std::memory_order_release);')
        text = replace_once(text, '  local_writer = &registration.producer;\n  return true;',
            '  local_writer = &registration.producer;\n'
            '  phase_cost::attach(registration.producer.slot_, GetCurrentThreadId());\n  return true;')
        text = replace_once(text, 'void detach_current_thread() noexcept {\n',
            'void detach_current_thread() noexcept {\n  phase_cost::detach();\n')
    elif name == 'content/web_impl_win/WebThreadImpl.cpp':
        # Preserve the selected timer's real due before its existing reset.
        text = replace_once(text,
            '        WebTimerBase* timer = static_cast<WebTimerBase*>(selected->value);\n        timer->m_nextFireTime = 0;',
            '        WebTimerBase* timer = static_cast<WebTimerBase*>(selected->value);\n'
            '        const double diagnosticDue = timer->m_nextFireTime;\n        timer->m_nextFireTime = 0;')
        text = replace_once(text, '        willProcessTasks();\n        timer->fired();\n        didProcessTasks();\n        ++completedTasks;',
            '        willProcessTasks();\n'
            '        phase_cost::observe_lateness(phase_cost::model::metric::task_due_late_us,\n'
            '            diagnosticDue, [] { return WTF::monotonicallyIncreasingTime(); });\n'
            '        timer->fired();\n        didProcessTasks();\n        ++completedTasks;')
    elif name == 'content/browser/SharedTimerWin.h':
        for indent in ['    ', '        ']:
            before = indent + 'cancelSharedTimerDeadline();\n' + indent + 'CheckReEnter::incrementEnterCount();'
            after = indent + 'const double diagnosticDue = armedDeadline;\n' + before + '\n'
            after += indent + 'phase_cost::observe_lateness(phase_cost::model::metric::shared_wake_late_us,\n'
            after += indent + '    diagnosticDue, [] { return WTF::monotonicallyIncreasingTime(); });'
            text = replace_once(text, before, after)
    elif name == 'cc/raster/RasterTask.cpp':
        text = replace_once(text, '        raster();\n        if (m_receipt.finish())',
            '        content::phase_cost::scope phase(content::phase_cost::model::metric::raster_qpc);\n'
            '        raster();\n        (void)phase.finish();\n        if (m_receipt.finish())')
    elif name == 'cc/trees/LayerTreeHost.cpp':
        text = replace_once(text,
            '                const SkBitmap& bitmap = device->accessBitmap(false);\n                if (m_hasTransparentBackground && valid)',
            '                content::phase_cost::scope phase(content::phase_cost::model::metric::spare_copy_qpc);\n'
            '                const SkBitmap& bitmap = device->accessBitmap(false);\n                if (m_hasTransparentBackground && valid)')
        text = replace_once(text, '                next->canvas->drawBitmap(bitmap, 0, 0, nullptr);',
            '                next->canvas->drawBitmap(bitmap, 0, 0, nullptr);\n                (void)phase.finish();')
    elif name == 'content/browser/WebPageImpl.cpp':
        text = replace_once(text, '    if (useLayeredBuffer && !isCreatedThisTime)\n',
            '    phase_cost::scope mirrorPhase(phase_cost::model::metric::ui_mirror_copy_qpc);\n'
            '    if (useLayeredBuffer && !isCreatedThisTime)\n')
        text = replace_once(text, '\n    }\n\n}\n\nbool WebPageImpl::isDrawDirty()',
            '\n    }\n    (void)mirrorPhase.finish(device ? phase_cost::model::outcome::returned\n'
            '        : phase_cost::model::outcome::api_failed);\n\n}\n\nbool WebPageImpl::isDrawDirty()')
    elif name == 'skia/ext/bitmap_platform_device_win.cc':
        text = replace_once(text, '                                               const RECT* src_rect) {\n',
            '                                               const RECT* src_rect) {\n'
            '  content::phase_cost::scope phase(content::phase_cost::model::metric::native_submit_context_qpc);\n')
        text = replace_once(text, '  return draw_succeeded && batch_succeeded;',
            '  (void)phase.finish(draw_succeeded && batch_succeeded ? content::phase_cost::model::outcome::returned\n'
            '      : content::phase_cost::model::outcome::api_failed);\n  return draw_succeeded && batch_succeeded;')
        text = replace_once(text,
            'bool BitmapPlatformDevice::DrawToNativeLayeredContext(HDC dc, const RECT* src_rect, const RECT* client_rect)\n{\n',
            'bool BitmapPlatformDevice::DrawToNativeLayeredContext(HDC dc, const RECT* src_rect, const RECT* client_rect)\n{\n'
            '    content::phase_cost::scope phase(content::phase_cost::model::metric::native_submit_layered_qpc);\n')
        text = replace_once(text, '    return b;\n}\n\nconst SkBitmap& BitmapPlatformDevice::onAccessBitmap()',
            '    (void)phase.finish(b ? content::phase_cost::model::outcome::returned\n'
            '        : content::phase_cost::model::outcome::api_failed);\n'
            '    return b;\n}\n\nconst SkBitmap& BitmapPlatformDevice::onAccessBitmap()')
    elif name == 'wke/wke.cpp':
        text = replace_once(text,
            'void WKE_CALL_TYPE wkeSetDebugConfig(wkeWebView webview, const char* debugString, const char* param)\n{\n',
            'void WKE_CALL_TYPE wkeSetDebugConfig(wkeWebView webview, const char* debugString, const char* param)\n{\n'
            '    if (!webview && debugString && strcmp(debugString, "privateRenderPhaseModeV1") == 0) {\n'
            '        (void)content::phase_cost::request_mode(param); return;\n    }\n')
        text = replace_once(text,
            'void* WKE_CALL_TYPE wkeGetDebugConfig(wkeWebView webview, const char* debugString)\n{\n',
            'void* WKE_CALL_TYPE wkeGetDebugConfig(wkeWebView webview, const char* debugString)\n{\n'
            '    if (!webview && debugString && strcmp(debugString, "privateRenderPhaseSnapshotV1") == 0)\n'
            '        return const_cast<char*>(content::phase_cost::final_snapshot_json());\n')
    else:
        raise AssertionError('Unlisted source')
    return text.encode('latin1')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel-source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    source = args.kernel_source.resolve(strict=True)
    output = args.output.resolve()
    assert args.output.is_absolute() and not output.exists()
    assert not output.is_relative_to(REPOSITORY.resolve()) and not output.is_relative_to(source)
    baseline = json.loads((HERE/'source-baseline.json').read_bytes())
    assert baseline['schema'] == 'soui.private-render-phase-source/v1' and len(baseline['files']) == 8
    inputs, changed, patch = [], [], []
    for row in baseline['files']:
        path = source/row['path']
        assert path.is_file() and not path.is_symlink()
        raw = path.read_bytes(); assert digest(raw) == {k:row[k] for k in ['bytes','sha256']}
        updated = transform(row['path'], raw)
        assert updated != raw
        inputs.append((row, raw)); changed.append((row['path'], updated))
        patch.extend(difflib.unified_diff(raw.decode('latin1').splitlines(True),
            updated.decode('latin1').splitlines(True), fromfile='a/'+row['path'], tofile='b/'+row['path']))
    # Every transformation succeeded before creating an output directory.
    output.mkdir(parents=True)
    files=[]
    for name, data in changed:
        destination=output/'kernel'/name; destination.parent.mkdir(parents=True,exist_ok=True)
        with destination.open('xb') as stream: stream.write(data)
        files.append({'path':'kernel/'+name, **digest(data)})
    for name in ['RenderPhaseCost.h','RenderPhaseCostHooks.h','RenderPhaseCostHooks.inc']:
        data=(HERE/name).read_bytes(); destination=output/'include'/name
        destination.parent.mkdir(parents=True,exist_ok=True)
        with destination.open('xb') as stream: stream.write(data)
        files.append({'path':'include/'+name,**digest(data)})
    patch_data=''.join(patch).encode('latin1')
    with (output/'diagnostic-overlay.patch').open('xb') as stream: stream.write(patch_data)
    for row, raw in inputs: assert (source/row['path']).read_bytes() == raw
    report={'schema':'soui.private-render-phase-overlay/v1','baseline':baseline,'files':files,
        'patch':digest(patch_data),'vendorSourceChanged':False,'defaultSdkChanged':False,
        'compiled':False,'guiExecuted':False,'performanceGate':False}
    with (output/'overlay.json').open('x',encoding='utf-8',newline='\n') as stream:
        json.dump(report,stream,indent=2);stream.write('\n')
    print(json.dumps({'preparedDiagnosticOverlayOnly':True,'sources':8,'translationUnits':7,
        'output':str(output),'manifest':digest((output/'overlay.json').read_bytes())}))

if __name__ == '__main__':
    main()
