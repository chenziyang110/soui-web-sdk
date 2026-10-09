# Current Win32 kernel link recovery, 2026-10-10

Root executed the sealed V5 recovery entry with the fixed Hostx64/x86 LINK and the original 3,024 objects, one resource and 34 named libraries. LINK and all three PE checks naturally returned root/supervisor 0/0, with no forced Job cleanup. LINK PID109488 / creation FILETIME134360444316395903 completed in 41,813ms. The resulting DLL is 23,968,768 bytes, SHA-256 `de9178744bcf6b187312726d13e1ae5ebd9389ebad2589e70f4789681458afc1`, PE machine332, with 9,836 exports including the required controlled broker and interaction APIs. The DLL itself is a hash-only reference and is not included here.

The final LINK image query returned null/error5 after earlier fixed-image observations for the same PID/FILETIME. V5 records that unreadable observation separately; it does not invent an image. The strict exit, timing, identity and closure rules remain unchanged. Root checked the result and an independent agent reviewed its original raw records and pins.

The original source, protected inputs and selected tool/library inventories were checked before and after execution and remained unchanged. The original 5,234-file inventory and one separately pinned pre-existing hidden zero-byte file are retained; the exact Windows cache junction was validated without following it. These observations do not replace complete compiler input provenance.

The historical full-build failure, V1 preflight failure, V3 missing-resource-compiler failure and V4 controller reader failure remain failed. V4 had already generated a private DLL, but its failed controller result is not promoted to the V5 result. V5 is a new actual execution. This archive contains 90 byte-identical control or raw result records and a manifest; executable entry copies have the inert `.raw` suffix. Original private paths in the records are retained as observations, and this is not a portable replay or complete source package.

Full recompilation, complete toolchain provenance, ABI execution, native behavior, Win7/PE, resource/performance/long-duration qualification and formal release remain unverified. `qualificationPassed=false` and `releaseReady=false`.
