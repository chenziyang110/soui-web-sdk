# 0.5 scope integration, 2026-10-09

`index.json` binds 79 byte-preserved files to actual compilation and native tests on the current Windows development host. Main `e4ff7f9d1e670550a62a402bd1bf1888c0a7546d` was integrated before validation. Compiled source identity is `61b743bf388a5e5b6ceb2794d792dca755dc68883465b5a8f9a57793fad47a46`.

Both x86 and x64 Release SDK builds completed through standard Build.ps1, with WebView2 and installation. Four repository-internal tests per architecture were compiled from an external fixture against that installation and all eight executables naturally completed with empty owned Jobs. Their identities, project metadata, context and raw supervision streams are retained. The fixture uses private test seams and is not an independent public-SDK adoption consumer.

Four compilation roots returned zero. Their remaining compiler descendants were cleaned up by the generic v1 supervisor, so its false results are preserved. This archive claims no compiler-tail exception, GUI execution, local Agent transport, target OS coverage, long-session or final release acceptance. Executable/library bytes remain local, identified by SHA-256 in the reports. ADR and archival metadata updates occurred after compilation and do not inherit its source identity.
