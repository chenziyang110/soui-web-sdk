# Public Miniblink LLVM adapter

SOURCE candidate only. No build or test has run; no compiler, WholeJob, ABI, OS or release qualification is granted.

The default MSVC143 command keeps the previous eleven libraries followed by Main and the original dependency-build/v1 report. LLVM22MSVC142 is an explicit first lane: Component Miniblink, Release, GPU off, public x86 or x64, one architecture per invocation. LLVM All, SOUI and EnableExperimentalGpu are rejected before legacy MSVC discovery. Other public Build/SDK LLVM lanes are separate.

Inputs are the existing LlvmRoot, MsvcRoot, WindowsSdkRoot, NinjaExe, WindowsCmdExe and PowerShellExe plus VsInstallDir. Select LLVM 22.1.3, MSVC 14.29.30133 owned by that VS installation, and Windows SDK 10.0.19041.0. VsInstallDir must contain AMD64 MSBuild/Current/Bin/amd64/MSBuild.exe and VC/Auxiliary/Build/vcvarsall.bat; selected MASM is Hostx64/x86/ml.exe or Hostx64/x64/ml64.exe. All paths must be absolute, have no reparses and contain no cmd/MSBuild metacharacters. Spaces and parentheses are supported.

Example after genuine Source review and composition, with each variable set to an actual checked absolute path:

~~~powershell
$build = @{
  Component = 'Miniblink'
  Architecture = 'x64'
  NativeToolchain = 'LLVM22MSVC142'
  VsInstallDir = $vs
  LlvmRoot = $llvm
  MsvcRoot = $msvc142
  WindowsSdkRoot = $sdk19041
  NinjaExe = $ninja
  WindowsCmdExe = $windowsCmd
  PowerShellExe = $powerShell
  OutputRoot = $newExternalOutput
}
& ./tools/Build-Dependencies.ps1 @build
~~~

OutputRoot must not exist, even as an empty directory, and must be disjoint from SDK/native source and selected tool roots. A later x86 run uses a different absent output directory. Win32 keeps node.dll; x64 keeps miniblink.dll. The existing libraries/lib and miniblink/bin layouts remain. No historical library, OBJ, PCH, temp output, private controller, nonce or request is imported.

The original five/six cmd arguments remain in order. LLVM appends six arguments: profile, VS root, LLVM root, MSVC root, SDK root, fixed sibling targets. The internal branches use five/six SHIFT operations before reading the new %1-%6 vector. The public caller chooses targets from its own tools directory, not an executable caller config.

The portable targets retain explicit compiler/include/lib/CRT rules, each Main TU/PCH diagnostic definition, Win32 V8 __chkstk on actual Main Link, Win32 Skia SSE2, original SSSE3/SSE4.1 TU options and default CRT ignore strategy, and selected MASM/RC/MT. They omit Root metadata dumps. If either V8 project requires a forced-symbol set other than the reviewed __chkstk, the data check rejects the build rather than deleting another required symbol.

LLVM emits kernel-llvm-inputs.json and complete native kernel-source-before/after.json alongside build-Miniblink.json. The inputs distinguish the existing profile InputIdentity from independent VS owner-role pins. Current recipe/tool files and each of the eleven newly produced library pins are rechecked before Main and at completion. The dependency record adds kernelLlvm file bindings and observed source/input stability. compilerExecution, wholeJob, ABI and toolchainQualification remain null; releaseReady remains false. A bounded selected clang-cl --no-default-config --version probe rejects versions other than 22.1.3 and records independent raw stdout/stderr and actual executable before/after pins. Its response is not translation-unit compilation evidence. Requested version strings and a zero process exit do not establish compiler or WholeJob proof.

A modern Windows x64 build host with the selected VS/MSBuild and SDK is required. Win7/PE target support, CPU dispatch and OS runtime support need the actual target matrix; none has passed through this candidate. The existing V8 SSE2/CMOV CPU boundary and three special Skia ISA TUs remain. This packet must not be reported as compatible with all Windows systems.

Root later runs the public CLI on an exact NoGit full-source extraction under its existing Owned lane, with original budgets, busy/abandoned refusal and forced-cleanup failure rules, and verifies natural closure plus real compiler/COFF/MAP/current-library evidence. Its private full24 artifacts cannot replace the public build. Current licensing/SBOM/binary applicability and other 0.5 P0 remain separate; the existing SDK signing waiver remains.

This is a separate adapter document. It does not replace SOURCE_REBUILD.md or the other slice's final e45/d072 document, SourceRoots material composition, public source manifests or receipts.

Declared tests: thirteen data/static cases in tests/public-kernel-llvm-inputs.test.ps1. They remain unexecuted. A later controlled logical workspace must supply CandidateSource (composed candidate tools) and DependencySource (pinned original 7df stock tools for unchanged functions/default-body basis). Synthetic PE files are header-data fixtures only, never native tool/compiler proof.
