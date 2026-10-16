# ADR-0080: Direct compiler database classification and RC command bindings

Status: proposed; Source/pure-data checks only. Actual SDK rebuild and current RC/MT process proof remain required. Qualification and release readiness remain false.

## Problem and evidence

Actual x86 attempt `attempt-8a6aaea6-b9b3-4d6a-b058-10505bf616e9`, based on commit `8304f339e4c083a74cb9e08731b7c296c5a277d1`, completed its 195 C++ build but failed observation. All 124 selected Release database rows were sent into a clang-only gate. The first non-clang row, selected index 39/full database index 117, is the starter `demo.rc` command with `rc.exe` argv0. Its complete 38-element vector parses correctly; the clang basename gate returns null before alias normalization. The failed attempt remains failed, and its unit/x64 stages were not run.

The real default Ninja graph has resource rows whose commands use CMake's `cmcldeps.exe RC` launcher. Substring candidacy can also match a link command's `--rc=` declaration and resource input after a short RC path has been physically authenticated. The launcher binary was absent from the prelaunch dependency envelope. These gaps require product observation changes, rather than removing RC rows from the database or manufacturing tool execution evidence.

## Decision

Keep every selected Release database row. Parse its entire CRT argument vector and admit only a direct `clang-cl.exe` or `rc.exe` compiler. Reject unknown programs, shell operators and response files. Each compiler argv0 is physically authenticated through the existing fixed `Assert-ToolFileIdentity` helper before creating its alias. C++ continues through the existing flags, unique query/verbose, object, default ALL, header dependencies, incremental source/header pins and architecture checks.

For RC, independently validate current build cwd, drive-absolute local `.rc` source (`^[A-Za-z]:[\\/]`, checked before source normalization), owned `.res` output, one exact `/fo` operand and final source operand. `IsPathRooted` is insufficient on Windows: root-relative and drive-relative sources can resolve to the same frozen source through ambient drive state. Both forms must reject. Match the entire normalized tool-plus-argument vector to exactly one database record and its unique RC graph edge. Unqueried declarations remain explicitly outside the selected default ALL graph and cannot become compiled proof. Every `.res` input of a selected binary must exist in the complete resource binding map; missing resources must fail rather than disappear from its input-resource proof.

Accept a direct RC query, or only this fixed wrapper prefix (zero-based slots):

| Slot | Meaning |
|---|---|
| 0 | selected CMake directory's authenticated `cmcldeps.exe` |
| 1 | exact `RC` |
| 2 | same resolved RC source |
| 3 | same resolved output plus `.d` |
| 4 | same resolved RC output |
| 5 | exact `Note: including file: `, including trailing space |
| 6 | authenticated pinned clang preprocessor |
| 7 | authenticated pinned RC compiler |
| 8 onward | complete remaining direct database RC argument vector |

Freeze `cmcldeps.exe` from the actually selected CMake executable's directory before Build. Include this independent pin in the whole before/after dependency envelope and original standard-build inputs; pass it to the observer and bind it to the serialized observation. Current and prior standard-build readers check the selected CMake sibling, whole inventory membership and exact observation pin. This does not add a profile ToolPins role or change any native POD ABI.

Retain complete raw database/query/verbose text. Current RC proof requires a valid Ninja progress-prefixed record whose entire parsed vector matches the generated command, with at most one observation. Reuse requires the existing authenticated prior natural observation, exact database and generated vectors, source/output bytes and hashes, plus every pre-frozen dependency unchanged; it does not claim an RC header-read trace. Prior reader code pins remain mandatory.

## Proof boundaries

File identity proves that an observed executable path refers to the pinned physical file. It does not prove that the tool executed. Existing current RC and MT Owned-role gates, whole Job natural closure, STOP rejection and all frozen dependency controls remain unchanged. Missing short-lived RC/MT process observations remain a separate fail-closed gap. This change cannot retrospectively qualify the failed attempt.

The existing PS test entry loads only actual pure functions from Source AST; its alias map is explicitly synthetic and no identity helper is loaded. Existing JS tests use inert file-data pins for selected wrapper membership and mutation rejection. Neither path executes a compiler, SDK, native helper or VM, and neither can close the actual process proof or SDK acceptance gate.
