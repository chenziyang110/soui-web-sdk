# 0099 — Capture short lived installed consumer build tools

Status: implemented; Source16 x86/x64 installed consumer builds and complete native reload entries verified with natural closure.

## Context

Issue #96's real fresh installed acceptance build compiled and linked, but the
semantic observer required a manifest process absent from its polling history.
Independent native debug capture subsequently proved that the tool did not run:
the actual CMake 4.2.3 nonincremental link embedded its manifest directly through
the linker. A configured `--mt` path alone is not an invocation. Ordinary SDK
builds already preserve an independent Windows debug event stream alongside the
original owned process stream.

## Decision

Add a separate version 3 native observer input for installed consumers. It admits
only the selected, pinned AMD64 CMake executable and the exact fresh consumer
build command. The input fixes the consumer kind, owned build directory,
architecture and configuration. Acceptance and release directories retain their
existing boundaries. Version 2 continues to admit only the original Build.ps1
entry. No arbitrary executable, target, extra build option or alternate path is
introduced.

Keep the outer observer and inner CMake identities separate. Validate original
raw streams and whole job closure both when producing and when reading consumer
evidence. The existing compiler, object, resource, archive, linker and installed
SDK identity checks remain mandatory. Polling records are preserved unchanged;
the debug stream supplies additional physical process and full command evidence.

The observer uses its independently prepared host64 owned runner, including when
building an x86 SDK consumer. Configure, semantic observation and application
execution retain the selected product architecture's stock runner. Bind the build
runner to the verified SDK observer producer and current physical file; require
its exact original capture in both writer and reader. Equating these two runners
incorrectly rejected the first actual x86 consumer before compilation.

For installed consumers only, recognize the exact CMake 4.2.3 `vs_link_exe`
wrapper with MSVC version greater than 1600, one explicit nonincremental flag,
empty user manifest list, no response file or additional shell command. Bind all
five tool paths to physical pinned files and the CMake product version to the
actual configuration version. This path embeds through the linker and does not
require a separate MT process. Other versions, shapes and modes retain the
existing conservative MT requirement. The complete native history remains
mandatory; configured tools never become synthetic execution rows.

The behavior is defined by the matching upstream
[CMake 4.2.3 implementation](https://github.com/Kitware/CMake/blob/v4.2.3/Source/cmcmd.cxx),
specifically `cmVSLink::Parse` and `LinkNonIncremental`.

## Alternatives

Retrying until polling happens to see mt.exe is nondeterministic. Sleeping in the
tool process changes execution solely to influence the collector. Removing the
manifest process requirement loses provenance. Reusing the SDK entry schema for
CMake would obscure the actual root. These alternatives are rejected.

## Compatibility and migration

Public SDK APIs and supported target platforms do not change. New installed
consumer runs require a freshly compiled observer and current accepted SDK build.
Old polling observations remain readable through their original version 1 path;
they cannot be relabelled as dual root evidence. No compatibility or release
qualification follows from this collector change alone.

## Verification

Run parser and stream boundary tests covering both consumer kinds and SDK mode,
root swaps, target/argument/path tampering, forced cleanup and incomplete history.
Compile the changed observer, then run a fresh SDK and actual installed consumer
build, including independent rereading of the resulting evidence. Preserve all
failed attempts. Continue the resource and release gates on the accepted build.
