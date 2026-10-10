# Public json_bridge endpoint composition

These six cases link the normal Soui::Sdk and its public
ActionJson target. They use actual json_bridge registrations, bridge-owned
stopped endpoint creation, production named pipes, native authorization and the
same registered task service. No private macro/store/alternate registry,
TestKit or clock override is used. The small existing public client body is
copied byte-for-byte and its source interval is recorded in the candidate.

Creation freezes method/task/policy registration. It does not start I/O or
grant authority. The returned pointer is borrowed, must not be deleted, and is
invalid when json_bridge is destroyed. Explicit stop/poll must finish before
bridge destruction. Task-handler context remains alive until the bridge's
task-service destructor/join returns; endpoint stopped alone is not a business
barrier. Pending native I/O or remote-thread failure exits the test process
under external Owned supervision rather than releasing borrowed storage.

Cases cover all registration/policy gates, an actual factory allocation failure,
post-bootstrap rejection, typed action/page isolation, projected family results
and real disconnect cancellation. Existing endpoint peer/framing tests retain
their original scope. TIMEOUT 15 is a failure boundary, not cancellation proof.
Native V10 executed all six cases against the normal SDK on the current Windows
host for x64 and Win32, with no failed or skipped cases and natural
root0/supervisor0. Records are out/native-v10/x64/summary.json and
out/native-v10/Win32/summary.json. The relocated installed consumer separately
checks the stopped factory symbol/layout and policy; it does not exercise a live
pipe. These results bind the recorded working-tree sources, so a changed or
standalone commit snapshot needs its own build/install composition check. No
Win7, cross-account, long-run, full-SDK or release qualification is added.
