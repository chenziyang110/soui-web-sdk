# Current export decoder and MessagePort stage analysis — prepared only

Root independently reviews these scripts before running either. Both are offline file readers; neither executes guest/controller/SDK code or controls the VM. They have not been executed during preparation.

`Decode-CurrentExport.cjs` derives from the original `tray-build/decoded-tray2/Decode-TrayExport.cjs`, whose exact identity is recorded in Derivation.prepared.json. It preserves its canonical Base64, strict UTF-8, exact ordered JSON envelope, file byte/SHA, Windows path alias/confinement, unique selected frame and closed raw-output checks. The catalog count is1–512 rather than a fixed9. It accepts only current `mp77c-<stamp>-stage/run` or `wb31c-<stamp>-stage/run` evidence paths and the unchanged serial exporter identity. The raw files plus catalog remain bounded by8MiB. It saves only the exact selected BEGIN/data/END frame, envelope and listed files; never the whole serial log or other tags.

Example after Root review, replacing SERIAL_LOG with the current VM run's actual serial.log path:

```text
node Decode-CurrentExport.cjs SERIAL_LOG ISSUESHORT57E29AD8A12244A0AC4816B72DF56AE5 F:/work/SOUI_WEB/out/open-issues-closeout-20261029/current-decoded/mp-a1-stage
```

The output must be a new direct child of `current-decoded`, with a simple lowercase name. Existing outputs are preserved. Appending serial data after the open snapshot does not enter this decode; an incomplete, duplicate, malformed, oversized or mismatched selected frame is refused. Transport success always leaves `testPassed:null` and production flagsfalse.

`Analyze-MPStage.cjs` is a separate offline check for the already prepared current MessagePort manifest SHA `4195e228c9d09ca92d1dba20986058feda5669929da5119d953be6bcebc025b1`. It checks the complete decoder index/raw catalog,11 original exported files, actual PS/wrapper return0 and environment/lease records, precise absent PID+birth observations,13 source/copy reports and every architecture's runtime source/copy report against the hosted manifest. It emits the original Stage.actual.json SHA without changing its bytes or the decoded transport tree. It does not verify current live guest files, execute a product, establish whole Owned Job cleanup, or turn staging into a MessagePort behavior pass.

```text
node Analyze-MPStage.cjs F:/work/SOUI_WEB/out/open-issues-closeout-20261029/current-decoded/mp-a1-stage F:/work/SOUI_WEB/out/open-issues-closeout-20261029/message-port-current/light-payload-e4044c6a-1537-4da3-a96b-063bcd90adbb/payload/Current.manifest.json F:/work/SOUI_WEB/out/open-issues-closeout-20261029/current-analysis/mp-a1-stage
```

The analysis output is a new direct child of `current-analysis`; it is separate from the immutable decoder output. Root must inspect its actual result and the original stage evidence before the separate guest run command. No test or issue is closed by successful transport/staging analysis.
