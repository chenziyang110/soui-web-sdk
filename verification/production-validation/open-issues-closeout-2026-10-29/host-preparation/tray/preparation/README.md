# Public tray consumer current build preparation

Output-only preparation for missing Issue27/29 VM short tests. This directory
does not modify SDK sources, installed prefixes, frozen release artifacts, or
the Git index. Its presence is not compilation or runtime success.

`source/CMakeLists.txt` and `source/main.cpp` are byte-exact copies from the
current C:/sc07/tests/tray-consumer. The public installed SDK prefixes are
C:/sr06/out/release-install-{x64,x86}-release. Build is independent of the SDK
source tree and links the installed public Soui::Sdk target.

Once compiler preflight is genuinely clear, run this preparation exactly once:

    node F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-build/Run-TrayBuild.cjs

The wrapper creates a fresh attempt and C:/sci8-<attempt8>-{x64,x86} workspaces.
It serially configures/builds each one-TU consumer under the existing current
MSVC lifecycle helper with -timeout:20, original120000ms root and2000ms drain.
It pins current source, selected tools, sealed Owned runner and complete
installed SDK file identities before/after, and checks actual CMake selection,
PE architecture, compiler/link tracking logs, helper member births and natural
whole-Job completion. It never runs the output EXE or terminates preexisting
helpers. Initial busy observations are preserved in deferred-compiler-*.json.

After successful build, hand off both EXEs plus their selected app-local MSVC
runtime DLLs from the same current release runtime, with exact pins and a fresh
read-only ISO to the task VM's short C:/issue... directory. Root owns ISO mount,
VM input, real tray/shortcut/menu gesture, native Owned capture and COM1 export.
Do not use old preview tray EXEs or simulated callback messages as the current
test. Runtime/DLL dependency list still requires inspection of each built PE.

No product GUI has been executed by this preparation. Real Shell/Explorer,
Win7, multi-window task isolation, resources and long-run qualification remain
unverified until their own actual evidence is recorded.
