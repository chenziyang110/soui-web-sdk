"""Compile the actual production method against a deterministic GDI model.

Run from a configured MSVC developer environment; no product library is built.
Output must be a new directory outside the repository.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-file', type=pathlib.Path)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--compiler', default='cl.exe')
args = parser.parse_args()
here = pathlib.Path(__file__).resolve().parent
repo = here.parents[2]
source = (args.source_file or repo / 'third_party/sources/miniblink/skia/ext/bitmap_platform_device_win.cc').resolve()
output = args.output.resolve()
if output == repo or repo in output.parents:
    parser.error('--output must be outside the repository')
if output.exists():
    parser.error('--output must be a new directory; existing evidence is preserved')
compiler = shutil.which(args.compiler)
if compiler is None:
    parser.error('compiler unavailable; run from the configured MSVC developer environment')
raw = source.read_bytes()
start_marker = b'bool BitmapPlatformDevice::DrawToNativeContext('
end_marker = b'bool BitmapPlatformDevice::DrawToNativeLayeredContext('
if raw.count(start_marker) != 1 or raw.count(end_marker) != 1:
    parser.error('expected unique bounded production method signatures')
start = raw.index(start_marker)
end = raw.index(end_marker)
if end <= start:
    parser.error('production method boundary order is invalid')
method = raw[start:end]
output.mkdir(parents=True)
(output / 'method-under-test.inc').write_bytes(method)
manifest = {'schema': 'native-submission-source-binding/v1', 'source': str(source),
            'sourceSha256': hashlib.sha256(raw).hexdigest(),
            'methodSha256': hashlib.sha256(method).hexdigest(),
            'byteStart': start, 'byteEndExclusive': end,
            'testSha256': hashlib.sha256((here / 'submission_completion_tests.cpp').read_bytes()).hexdigest(),
            'compiler': compiler, 'fakeGdi': True, 'historicalCrcCauseProven': False}
(output / 'source-binding.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
exe = output / 'submission-completion-tests.exe'
command = [compiler, '/nologo', '/std:c++17', '/EHsc', '/W4', '/O2', '/MT', '/utf-8',
           '/I' + str(output), '/Fo' + str(output / 'test.obj'), '/Fe' + str(exe),
           str(here / 'submission_completion_tests.cpp')]
build = subprocess.run(command, capture_output=True, text=True, cwd=output)
(output / 'build.json').write_text(json.dumps({'command': command, 'exit': build.returncode,
    'stdout': build.stdout, 'stderr': build.stderr}, indent=2), encoding='utf-8')
if build.returncode:
    print(build.stdout + build.stderr)
    sys.exit(build.returncode)
test = subprocess.run([str(exe)], capture_output=True, text=True, cwd=output)
(output / 'test.stdout.json').write_text(test.stdout, encoding='utf-8')
(output / 'test.stderr.txt').write_text(test.stderr, encoding='utf-8')
(output / 'test-result.json').write_text(json.dumps({'exit': test.returncode,
    'exeSha256': hashlib.sha256(exe.read_bytes()).hexdigest()}, indent=2), encoding='utf-8')
print(test.stdout.strip())
if test.returncode:
    print(test.stderr)
sys.exit(test.returncode)
