# Native tray-focus control evidence

This is a separate exact-byte snapshot of the one x64 native control, its prepared inputs and transport metadata. It does not close SDK issue #27/#29 or change the sealed current-execution snapshot.

The a2 control was valid but NIM_SETFOCUS returned FALSE: native exit1 / supervisor1, Job0, no force, cleanup confirmed. The offline stream verifier checks identity and closure; it does not turn this failure into success. The immediate last-error integer has no established extended-error contract here. This single VM observation does not establish global OS/API incompatibility or an SDK fix.

The a1 loader failure has a real screenshot and an explicitly labeled Root received-message observation. It has no exported guest report or proven Job closure. Original a1 files and old prepared identities remain preserved in the task VM/output roots.

Native raw events do not authenticate right-click/Escape. Root's supplied operation observation and the two screenshots are recorded separately. An unreadable member image and the lack of a foreground-process image/class remain explicit.

No product, compiler, VM or Git operation is performed by the archive copier. All production/SDK qualification flags stay false; testPassed=null in archive preparation is not a native-control success.
