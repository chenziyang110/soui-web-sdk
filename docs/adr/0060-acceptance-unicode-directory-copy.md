# ADR 0060 — acceptance directory copies on Windows Unicode paths

Status: Adopted after independent static review and seven actual filesystem regression passes. Native preparation replay remains pending.

The fixed Node v24.14.0 acceptance prepare actually received the correct Chinese suite in its Owned context. Its mkdir/writeFile path created the correct `kernel-input/node.dll`, but the first `cpSync` directory operation created a garbled sibling containing the six consumer files. The subsequent strict snapshot of the intended destination failed with ENOENT. That failure and both directories remain preserved.

The same Node version's primary `lib/internal/fs/cp/cp-sync.js` uses `cpSyncCopyDir` only when `opts.filter` is absent. That C++ function converts its JavaScript paths into UTF-8 `BufferValue` and then passes narrow strings to `std::filesystem` directory creation/path construction. This is a source-supported explanation for the Windows encoding split, not a new executed paired experiment or a defect in the observed Owned argv.

Decision: append only `filter:()=>true` to the acceptance runner's three existing recursive `cpSync` calls. A real filter selects the version's JS directory traversal (`opendirSync`, `join`, `mkdirSync`, `copyFileSync`) and includes every entry. Keep `recursive:true`, `errorOnExist:true`, `force:false`, the original sources/destinations, and every before/after snapshot and linked-path refusal. Do not rename paths to ASCII, replace the supervisor, upgrade Node, omit a file, or rewrite a failed receipt.

This filter is not an authorization or link-safety filter. Existing `delivery-identity` traversal and snapshots still refuse linked inputs/outputs. Overwrite behavior stays fail-closed. The regression directly evaluates the actual runner's prepare/stage functions against real Unicode filesystem fixtures; only unrelated build records and launch functions are unit-test fixtures/blocked dependencies. It does not launch a native consumer or prove a real SDK build. One further test executes the three exact actual copy statements to verify their overwrite policy; link rejection and post-copy drift checks exercise the existing actual runner paths.

Root must execute the fixed-version filesystem tests under the existing Owned supervisor, then derive a fresh acceptance input/source seal and replay preparation at the same Chinese path contract. Old failures remain false. Node versions, locales, other delivery copies, full resource scope, and OS-wide compatibility remain unqualified.

Primary fixed-version sources:
- https://github.com/nodejs/node/blob/v24.14.0/lib/internal/fs/cp/cp-sync.js#L125-L173
- https://github.com/nodejs/node/blob/v24.14.0/src/node_file.cc#L3192-L3348
- https://github.com/nodejs/node/blob/v24.14.0/lib/fs.js#L2948-L2954

Root executed the retained original and the candidate with the fixed Node/Owned supervisor. The original run returned root/supervisor 1/1, genericPassed=false, no forced cleanup, but reported only one file-level test failure; it supplied no seven-case assertion diagnostic or child crash code. The candidate returned natural 0/0 with all seven actual filesystem cases passing and nine retained fixture trees. The original Windows91 ENOENT/garbled-sibling evidence remains unchanged. These observations do not certify native SDK/resource/OS/release gates.
