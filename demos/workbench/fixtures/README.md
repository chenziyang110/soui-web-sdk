# FileInsight fixtures

These samples are native filesystem data. Workbench deploys the two sample
directories unchanged to `fixture-data/` beside its executable and exposes them
only through the existing permission-checked opaque root tokens. They are not
part of the `resources/` HTTP publication tree, whose ASCII path contract remains
unchanged. Web public media is built from `web/http-public/`; the older
`web/public/fixtures/` source copies are retained and are not published by Vite.

`fileinsight-fixture` is the deterministic five-file sample used by the
FileInsight demo. Its expected logical totals are 5 files / 21 bytes:

- `a.txt` (3 bytes), `B.TXT` (5 bytes), `nested/报告.txt` (6 UTF-8 bytes),
  `image.bin` (7 bytes), and `LICENSE` (0 bytes)

- `.txt`: 3 files / 14 bytes (`abc`, `hello`, UTF-8 `你好`)
- `.bin`: 1 file / 7 bytes
- `[none]`: 1 file / 0 bytes (`LICENSE`)

`fileinsight-fixture-alt` is a second checked-in fixture with different
contents: `alpha.md` (6 bytes), `data.json` (3 bytes), `nested/配置.ini`
(6 bytes), and `EMPTY` (0 bytes), for 4 files / 15 bytes. The UI can run it
through the same opaque token and native scan path, so the result visibly
changes when the selected fixture changes.

The native statistics smoke also creates a temporary fixture with different
contents, proving that the scanner does not contain hard-coded results.
