# Windows local endpoint finite native cases

Included from the existing native test parent. The target compiles only
endpoint_tests.cpp and links Soui::Sdk plus Soui::ActionJson. The actual SDK
provides windows_local_endpoint.cpp and its public include directory. This child
does not configure another SDK, compile a private provider overlay, prepend a
candidate include path or carry the historical source-pins manifest.

The 20 case names/arguments and TIMEOUT 15 remain the same as the Root-executed
x64/Win32 fixture. Cases exercise actual Windows pipes, peer identity, owner
dispatch, scoped revocation, bounded frames, reconnect/stop and a real child
process. Client creation failures retain their immediate GetLastError diagnostics.
Generic-write/create-instance alias access and competing server instances must
still be denied. No WILL_FAIL or expected-failure CTest conversion is present.

The public parent enables testing and makes Soui::Sdk/Soui::ActionJson available,
then includes this directory beside its existing children. Native V10 executed
all 20 cases against the normal SDK on the current Windows host for x64 and
Win32, with no failed or skipped cases and natural root0/supervisor0. The same
runs built, installed and relocated the public SDK for an independent consumer;
that consumer checks stopped factory ownership and does not open a live pipe.
Records are out/native-v10/x64/summary.json and
out/native-v10/Win32/summary.json. These bind their exact historical working-tree
sources; the intended standalone commit snapshot needs a new composition check.
Earlier private overlay compiler-tail partials remain generic failures and do
not establish the normal installed composition.

Finite same-host x64/Win32 cases do not qualify Win7 or other target systems,
cross-account/session deployment, a complete authorization UI, sustained bounds,
TypeScript Agent client deployment or full SDK release readiness.
