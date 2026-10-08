import hashlib
import json
import pathlib
import stat
import zipfile

root = pathlib.Path(__file__).resolve().parent
repo = pathlib.Path('C:/SOUI-SDK-development/preview9-tray')
archive = repo / 'dist/soui-sdk-0.3.0-preview.13-independent-build-attempt2-full-source.zip'
expected = json.loads((repo / 'source-manifest.json').read_text(encoding='utf-8-sig'))
assert expected['version'] == '0.3.0-preview.13'
command = json.loads((root / 'command-logs/source-package/result.json').read_text())
assert command['exitCode'] == 0 and not command['timedOut'] and command['error'] is None
with zipfile.ZipFile(archive) as handle:
    entries = [info for info in handle.infolist() if not info.is_dir()]
    names = [info.filename for info in entries]
    assert len(set(names)) == len(names)
    assert set(names) == {f['path'] for f in expected['files']} | {'source-manifest.json'}
    for info in entries:
        assert not stat.S_ISLNK(info.external_attr >> 16)
    actual = json.loads(handle.read('source-manifest.json').decode('utf-8-sig'))
    assert actual == expected
    for item in expected['files']:
        data = handle.read(item['path'])
        if item['normalization'] == 'lf':
            data = data.replace(b'\r\n', b'\n')
        else:
            assert item['normalization'] == 'none'
        assert len(data) == item['bytes'], item['path']
        assert hashlib.sha256(data).hexdigest() == item['sha256'], item['path']
sha = hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest()
assert pathlib.Path(str(archive) + '.sha256').read_text().split()[0] == sha
result = {'verified': True, 'archive': str(archive), 'sha256': sha,
          'bytes': archive.stat().st_size, 'manifestFiles': len(expected['files']),
          'zipFiles': len(names), 'cleanMachineReproduction': False, 'releaseReady': False}
with (root / 'zip-verification.json').open('x', encoding='utf-8') as output:
    json.dump(result, output, ensure_ascii=False, indent=2)
    output.write('\n')
print(json.dumps(result, ensure_ascii=False))
