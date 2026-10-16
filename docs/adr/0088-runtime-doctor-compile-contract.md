# 0088: Bind RuntimeDoctor to its standalone compile contract

Status: proposed, Source only. Independent review and a new actual SDK build are pending.

The original LLVM Release build compiled, linked and installed naturally with exit0/0 and an empty Job. The later semantic observer exited1 naturally. Its SDK source-prefix rule treated RuntimeDoctor's shared `sdk/src/host/runtime_profile.cpp` as an SDK library translation unit and demanded MD plus `/U_DEBUG`. The original attempt remains failed.

RuntimeDoctor's existing CMake target deliberately fixes MultiThreaded static CRT, Unicode, NOMINMAX, WINVER/_WIN32_WINNT0x0601 and console subsystem6.01. It diagnoses offline machines without requiring an installed VC Redistributable. Its C++ and CMake contract does not change.

The observer recognizes only these two source/object pairs under the current distribution and build roots and selected configuration:

- `tools/runtime-doctor/main.cpp` → `tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/<configuration>/main.cpp.obj`
- `sdk/src/host/runtime_profile.cpp` → `tools/runtime-doctor/CMakeFiles/SouiRuntimeDoctor.dir/<configuration>/__/__/sdk/src/host/runtime_profile.cpp.obj`

A Doctor target object with any other source, object shape or configuration rejects. The exact Doctor main source under another target also rejects. The shared source outside those pairs continues through the ordinary SDK prefix rule; its basename grants no exemption.

The Doctor command must contain exactly one explicit MT option, matching CMAKE_INTDIR, the actual Unicode/NOMINMAX and Win7 definitions, and no competing CRT or explicit fms-runtime library override. Repeated identical WINVER definitions from CMake and the fixed profile are retained; missing, undefining or conflicting macros reject. MD and MDd use the original predicate unchanged. Ordinary SDK sources retain the original MD, `/U_DEBUG` and Win7 macro gates; installed acceptance/release gates also remain unchanged.

All database/Ninja membership, source/object/header pins, current compiler identity, archive/link/import/resource/manifest evidence, original polling, complete held-process/debug history and natural closeout checks remain unchanged. This decision does not create an UnknownTarget bypass, skip SDK C++ sources or rewrite an old observation.

Source verification extracts only named pure functions with the PowerShell parser. It replays the exact original failing command as oldRED/new predicateGREEN, checks all41 original SDK-prefix Release rows plus the Doctor main row, and exercises ordinary SDK MD, invalid CRT/configuration/source/object/macro/override cases. It does not run the observer entry, compiler, Ninja, native identity helper, Owned, SDK or VM. Predicate success over old commands is not a new semantic build acceptance, OS compatibility or release qualification.

Root must independently review these exact three Source members, adopt them into a fresh SDK candidate with its final manifest, and perform a new full actual build and semantic observation. Original native successes, the failing postreader and whole failed attempt remain immutable.
