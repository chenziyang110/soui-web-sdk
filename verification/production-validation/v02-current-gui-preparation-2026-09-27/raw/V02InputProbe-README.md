# V02 operating-system input probe (preparation only)

`V02InputProbe.cpp` is an external acceptance helper. It has not been compiled or run. It does not alter the SDK, Demo, or frozen application binaries. Compile it only after the host has no other QEMU, SOUI, test, compiler, or desktop consumer workload.

For CMake, copy `V02InputProbe-CMakeLists.txt` to `CMakeLists.txt` in a fresh isolated helper source directory together with the C++ file, then configure separate x86/x64 build directories. Do not reuse an existing build directory or overwrite an attempt's evidence.

The helper finds a visible, unowned top-level window for a frozen PID, checks the process creation FILETIME, and refuses every input action unless that exact window is already naturally foreground. It never calls `SetForegroundWindow`, `BringWindowToTop`, or equivalent foreground-stealing APIs. `inspect` may use creation identity `0` once to record the actual identity; all later actions must supply that value. Each result path must be new and absolute.

`click` and `wheel` use OS mouse input at specified client coordinates. `pinyin` emits ordinary virtual-key events for lowercase ASCII letters followed by Space, so it cannot fake composition through Unicode injection or DOM assignment. The active input method and actual page composition counter must still be checked independently; the helper always records `imeCompositionVerified:false`. `resize-drag` uses the window's actual bottom-right border and requires both outer dimensions to increase. The accepted delta is frozen at `80,60`.

Suggested command shape: `V02InputProbe.exe PID CREATION_FILETIME inspect|click|wheel|pinyin|resize-drag ARG NEW_ABSOLUTE_JSON`. Failures and screenshots are evidence, not reasons to retry the same attempt silently. This is an injected OS-input path, not proof of a physical keyboard or mouse device. A successful action alone does not prove V02 page state, responsiveness, or production readiness.
