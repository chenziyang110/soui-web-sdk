# Acceptance thread sampler fault regression

This standalone Windows/MSVC test includes the real acceptance `main.cpp` and calls its `process_thread_count()` helper directly. It does not copy the helper or compile a copied SDK translation unit. Windows, SDK and standard headers are included before the four local Toolhelp API redirections; both production entry names are renamed only around the include. The test never invokes either acceptance entry point, creates a Host or runs an acceptance workload.

The fifteen cases cover complete and empty enumeration, target/foreign PID filtering, the minimum valid owner-field extent, Create/First/Next/Close failures, saved enumeration errors when Close also fails, invalid short/oversized records and exactly one Close after a valid snapshot. A failing helper must not return a partial count. Each case is `RUN_SERIAL`, has a 30-second CTest timeout and is an ordinary passing test with no `WILL_FAIL` classification.

Configure this directory independently with a normal installed Soui SDK, for example:

```powershell
cmake -S tests/acceptance-thread-sampler -B out/thread-sampler-x64 -G "Visual Studio 17 2022" -A x64 -DSouiSdk_DIR:PATH=<installed-sdk>/lib/cmake/SouiSdk
cmake --build out/thread-sampler-x64 --config Release --target soui_acceptance_thread_sampler_tests --parallel 2
ctest --test-dir out/thread-sampler-x64 -C Release --output-on-failure --no-tests=error
```

Use a separate Win32 build directory and matching Win32 installed SDK for that architecture. The target uses `/MD`, `/U_DEBUG`, `/EHsc` and `/utf-8`, matching the SDK's current CRT convention. `find_package` requires the public imported `Soui::Sdk`; this leaf is not registered under the source-SDK native test parent.

By default `SOUI_ACCEPTANCE_MAIN` resolves to `../acceptance-app/main.cpp`. A release verification may supply an absolute `-DSOUI_ACCEPTANCE_MAIN:FILEPATH=...` pointing to an immutable source snapshot and record that input's actual SHA together with CL.read/OBJ/LINK evidence. The persistent CMake does not pin a developer-specific path, compiler version or historical source hash.

The fault cases validate the real helper through controlled API results; OS, resource and long-run behavior still require actual acceptance validation.
