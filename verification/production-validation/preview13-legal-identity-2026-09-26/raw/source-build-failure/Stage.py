import hashlib
import json
import pathlib
import stat
import zipfile

root = pathlib.Path(__file__).resolve().parent
source = root / 'source'
archive = pathlib.Path('C:/SOUI-SDK-development/preview9-tray/dist/soui-sdk-0.3.0-preview.13-independent-build-attempt2-full-source.zip')
expected_sha = '7ddafb54fdf2bb41f0aa3faeaad72d74b6de018d6e7435c307b803866dcc64b5'
assert not source.exists()
with archive.open('rb') as data:
    assert hashlib.file_digest(data, 'sha256').hexdigest() == expected_sha
with zipfile.ZipFile(archive) as handle:
    files = [item for item in handle.infolist() if not item.is_dir()]
    names = [item.filename for item in files]
    assert len(set(name.lower() for name in names)) == len(names) == 33103
    for item in files:
        name = pathlib.PurePosixPath(item.filename)
        assert not name.is_absolute() and '..' not in name.parts and '\\' not in item.filename and ':' not in item.filename
        assert not stat.S_ISLNK(item.external_attr >> 16)
    manifest = json.loads(handle.read('source-manifest.json').decode('utf-8-sig'))
    assert manifest['version'] == '0.3.0-preview.13'
    assert set(names) == {f['path'] for f in manifest['files']} | {'source-manifest.json'}
    for item in files:
        target = source / pathlib.PurePosixPath(item.filename)
        assert target.resolve().is_relative_to(source.resolve())
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as output:
            output.write(handle.read(item))
record = {'archive': str(archive), 'sha256': expected_sha, 'files': len(names),
          'source': str(source), 'gitCheckout': False, 'guiExecuted': False, 'releaseReady': False}
with (root / 'stage.json').open('x', encoding='utf-8') as output:
    json.dump(record, output, indent=2)
    output.write('\n')
print(json.dumps(record))
