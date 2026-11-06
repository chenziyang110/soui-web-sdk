"""Copy a dedicated diagnostic consumer; do not modify the ordinary SDK host."""
import argparse
import difflib
import importlib.util
import json
from pathlib import Path

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('overlay',HERE/'prepare-overlay.py')
overlay=importlib.util.module_from_spec(spec);spec.loader.exec_module(overlay)

def main_source(raw):
    text=raw.decode('latin1');change=overlay.replace_once
    text=change(text,'#include "native_phase_capture.h"',
        '#include "native_phase_capture.h"\n#include "DiagnosticPhaseClient.h"')
    text=change(text,'  scalar_capture input_capture;',
        '  perf::diagnostic_render_phase::client render_phases;\n  scalar_capture input_capture;')
    text=change(text,'  return result;\n}\nstd::uint64_t filetime(',
        '  require(result.backend=="miniblink"&&result.input_trace&&result.budget==0&&result.frame_rate==60,\n'
        '    "This explicit diagnostic consumer requires MiniBlink/scalar input trace/default budget/rate60");\n'
        '  return result;\n}\nstd::uint64_t filetime(')
    text=change(text,'    input_capture.configure(settings.input_trace,settings.backend=="miniblink"?settings.miniblink:fs::path{});',
        '    render_phases.initialize(settings.miniblink,SOUI_RENDER_PHASE_EXPECTED_SHA256);\n'
        '    input_capture.configure(settings.input_trace,settings.backend=="miniblink"?settings.miniblink:fs::path{});\n'
        '    render_phases.after_trace_configured();')
    text=change(text,'    write_new(settings.output/L"identity.json",identity.dump(2)+"\\n");writer=std::make_unique<report_writer>(settings.output);',
        '    identity["privateRenderPhase"]=render_phases.identity();\n'
        '    write_new(settings.output/L"identity.json",identity.dump(2)+"\\n");writer=std::make_unique<report_writer>(settings.output);')
    text=change(text,'    const bool trace_complete=input_capture.finish(settings.output);if(!trace_complete)exit_code=3;',
        '    const bool trace_complete=input_capture.finish(settings.output);if(!trace_complete)exit_code=3;\n'
        '    if(!render_phases.save_final(settings.output))exit_code=3;')
    text=change(text,'require(name=="second-host"||name=="reload"||name=="hide-restore","E_ACTION_UNKNOWN");',
        'require(name=="second-host"||name=="reload"||name=="hide-restore"||name=="diagnostic-close","E_ACTION_UNKNOWN");')
    text=change(text,'      if(next.name=="second-host"){if(created_count.load()<2)create_slot();}',
        '      if(next.name=="diagnostic-close"){item.request_close();item.close_step();}\n'
        '      else if(next.name=="second-host"){if(created_count.load()<2)create_slot();}')
    return text.encode('latin1')

def cmake_source(raw):
    newline=b'\r\n' if raw.splitlines(True)[0].endswith(b'\r\n') else b'\n'
    added='''
# Explicit lab-only identity; the default SDK/application target never gets it.
set(SOUI_RENDER_PHASE_EXPECTED_SHA256 "" CACHE STRING "Actual diagnostic kernel SHA256 for this architecture")
string(LENGTH "${SOUI_RENDER_PHASE_EXPECTED_SHA256}" _phase_sha_length)
if(NOT _phase_sha_length EQUAL 64 OR SOUI_RENDER_PHASE_EXPECTED_SHA256 MATCHES "[^a-f0-9]")
  message(FATAL_ERROR "Build the diagnostic kernel first and freeze its actual SHA256")
endif()
target_compile_definitions(SouiSdkPerformanceHost PRIVATE
  SOUI_RENDER_PHASE_EXPECTED_SHA256="${SOUI_RENDER_PHASE_EXPECTED_SHA256}")
'''
    return raw+added.replace('\n',newline.decode()).encode('ascii')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();source=args.source_root.resolve(strict=True);output=args.output.resolve()
    assert args.output.is_absolute() and not output.exists()
    assert not output.is_relative_to(overlay.REPOSITORY.resolve()) and not output.is_relative_to(source)
    baseline=json.loads((HERE/'consumer-baseline.json').read_bytes())
    assert baseline['schema']=='soui.private-render-phase-consumer-source/v1' and len(baseline['files'])==8
    originals=[];files=[];patch=[]
    for row in baseline['files']:
        path=source/row['path'];assert path.is_file() and not path.is_symlink()
        raw=path.read_bytes();assert overlay.digest(raw)=={k:row[k] for k in ['bytes','sha256']}
        updated=main_source(raw) if row['path']=='main.cpp' else cmake_source(raw) if row['path']=='CMakeLists.txt' else raw
        originals.append((path,raw));files.append((row['path'],updated))
        if updated!=raw:
            patch.extend(difflib.unified_diff(raw.decode('latin1').splitlines(True),updated.decode('latin1').splitlines(True),
                fromfile='a/'+row['path'],tofile='b/'+row['path']))
    helper=(HERE/'DiagnosticPhaseClient.h').read_bytes()
    output.mkdir(parents=True);inventory=[]
    for name,data in files+[('DiagnosticPhaseClient.h',helper)]:
        destination=output/name;destination.parent.mkdir(parents=True,exist_ok=True)
        with destination.open('xb') as stream:stream.write(data)
        inventory.append({'path':name,**overlay.digest(data)})
    patch_data=''.join(patch).encode('latin1')
    with (output/'consumer.patch').open('xb') as stream:stream.write(patch_data)
    for path,raw in originals:assert path.read_bytes()==raw
    report={'schema':'soui.private-render-phase-consumer/v1','baseline':baseline,'files':inventory,
        'patch':overlay.digest(patch_data),'ordinaryConsumerChanged':False,'sdkChanged':False,
        'compiled':False,'kernelExpectedShaSupplied':False,'guiExecuted':False,'performanceGate':False}
    with (output/'consumer.json').open('x',encoding='utf-8',newline='\n') as stream:
        json.dump(report,stream,indent=2);stream.write('\n')
    print(json.dumps({'preparedDiagnosticConsumerOnly':True,'sourceFiles':len(inventory),'output':str(output),
        'manifest':overlay.digest((output/'consumer.json').read_bytes()),'compiled':False}))

if __name__=='__main__':main()
