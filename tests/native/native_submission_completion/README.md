# Native submission completion contract

`run.py` extracts the exact `BitmapPlatformDevice::DrawToNativeContext` method
from the production translation unit and compiles it against a small fake GDI
surface. It does not duplicate or rewrite the method implementation.

The fake GDI queue deliberately defers source reads until `GdiFlush`. Tests
check that the actual method drains issuing-thread work before returning the
source pixels, ends paint and releases a newly created DC after the drain, and
reports opaque/alpha draw failures and flush failures. Both cached/new DCs and
explicit/default source rectangles are covered. A failed draw must still drain
preceding batch work. Direct source reuse after the method returns exposes a
missing completion boundary deterministically.

This model checks the source-lifetime contract. It does not simulate a Windows
display driver, prove that real BitBlt defers a given transfer, establish the
cause of an earlier CRC failure, or measure performance.

Run from a configured Windows MSVC developer environment:

```
python tests/native/native_submission_completion/run.py --output ../native-submission-new-result
```

Use `--source-file` to bind a historical production snapshot and demonstrate a
failing baseline. `--compiler` selects an available compiler with MSVC argument
syntax. Source bytes, extracted method bytes, test source, build command, native
exit code, and executable hashes are preserved in the output directory.
