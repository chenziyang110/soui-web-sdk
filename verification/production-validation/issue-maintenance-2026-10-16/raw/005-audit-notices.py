import hashlib
import json
from pathlib import Path
import zipfile

base = Path(__file__).parent
repo = Path('F:/work/soui-issue96')
source = Path('C:/work/sm05-release-050')
facts_path = repo / 'verification/production-validation/0.5-source30-release-2026-10-16/scoped-results/RELEASE-FACTS.json'
facts = json.loads(facts_path.read_text(encoding='utf-8-sig'))
packages_path = Path('C:/work/sm05-source30-application-packages-2/VERIFIED.json')
packages = json.loads(packages_path.read_text(encoding='utf-8-sig'))

def sha(data):
    return hashlib.sha256(data).hexdigest()

def pin(p):
    data = p.read_bytes()
    return {'path': str(p), 'bytes': len(data), 'sha256': sha(data)}

notices = [source / 'THIRD_PARTY_NOTICES.md', *sorted((source / 'third_party/licenses').rglob('*'))]
notices = [p for p in notices if p.is_file()]
source_rows = [{'path': p.relative_to(source).as_posix(), 'bytes': p.stat().st_size, 'sha256': sha(p.read_bytes())} for p in notices]
openssl = 'third_party/licenses/d52/openssl/LICENSE.txt'
assert 'Original SSLeay License' in (source / openssl).read_text(encoding='utf-8')
rows = []
for item in packages['archives']:
    archive = Path(item['archive']['path'])
    actual = pin(archive)
    release = next(r for r in facts['applicationArchives'] if r['name'] == archive.name)
    assert actual['bytes'] == release['bytes'] and actual['sha256'] == release['sha256']
    assert actual['sha256'] == item['archive']['sha256']
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        assert len(names) == len(set(names))
        for notice in source_rows:
            member = 'resources/legal/' + notice['path']
            data = z.read(member)
            assert len(data) == notice['bytes'] and sha(data) == notice['sha256'], (archive.name, member)
        kernels = [{'path': name, 'bytes': len(z.read(name)), 'sha256': sha(z.read(name))}
                   for name in names if name.lower().endswith('node.dll')]
        assert len(kernels) == (1 if item['profile'] == 'compatibility' else 0)
    rows.append({'architecture': item['arch'], 'target': item['target'], 'profile': item['profile'],
                 'archive': {'name': archive.name, 'bytes': actual['bytes'], 'sha256': actual['sha256']},
                 'noticeFilesChecked': len(source_rows), 'openSslSsLeayTextPresent': True, 'kernels': kernels})
assert len(rows) == 6
report = {'schema': 'soui.issue39-published-notice-inventory/v1', 'version': '0.5.0',
          'scope': 'Read exact application ZIPs matching the published release hashes; verify retained notice bytes only.',
          'inputs': [pin(facts_path), pin(packages_path), pin(Path(__file__))],
          'sourceNotices': source_rows, 'applications': rows,
          'completeLinkedComponentInventory': False, 'redistributionTermsApproved': False,
          'firstPartyLicenseDecisionRecorded': False, 'licenseReviewComplete': False, 'releaseReady': False}
with (base / 'NOTICE-AUDIT.json').open('x', encoding='utf-8', newline='\n') as f:
    json.dump(report, f, ensure_ascii=False, indent=2)
    f.write('\n')
print(json.dumps({'applications': len(rows), 'noticeFilesPerApplication': len(source_rows),
                  'openSslSsLeayTextPresent': True, 'licenseReviewComplete': False}))
