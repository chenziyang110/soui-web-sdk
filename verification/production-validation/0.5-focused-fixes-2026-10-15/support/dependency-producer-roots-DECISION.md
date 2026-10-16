# 0104 draft: explicit producer roots with exact delivered source matching

Written before implementation, 2026-10-15. Current Source19 acceptance remains frozen.

The rebuilt dependency verifier currently assumes that its public producer ran from the same directory as the eventual SDK checkout. An isolated kernel build has a different source owner even when every delivered source byte is identical. Rewriting an old invocation or source receipt to the new directory would falsify provenance; rerunning the same compilation only to change its directory does not test different code.

Add an explicit v2 dependency selection containing producerRoot for both kernel and SOUI. Keep v1 unchanged for existing callers. Validate the original public command, source owner, tool inputs, complete pre/post source roster, generated libraries, DLLs and natural owned-process closure against each real producer root. Then independently require the entire delivered component source tree to have the same root-independent snapshot identity, including all file bytes and directories. No line-ending normalization, excluded files, allowed drift or fallback to a matching DLL name is accepted. Missing producer roots or changed producer/delivery contents fail.

Keep selected application compiler/tool inputs, exact six-DLL payload matching, actual desktop smoke and all other release gates unchanged. The original producer files must remain available. Store explicit delivery-source match metadata in the v2 result. Optional PowerShell producer-root parameters construct v2; omission retains v1.

Compatibility/migration: no SDK ABI, binary format or original report changes. Existing records retain their schema and meaning. New callers pass both producer roots explicitly (the local root is usable for an unchanged component). This is traceable relocation support, not an exception to source immutability or compilation checks.

Validation: v1 regressions; reject partial/relative/unknown root fields and wrong schemas; match identical source trees at different locations; reject modified/deleted/added files and empty directory changes; verify actual newly rebuilt kernel with its original records and exact adopted source, then run the full application/dependency pairing and desktop check.
