# #77: old-kernel shutdown source investigation

The old kernel has a concrete Worker idle-retirement liveness defect. The current 0.6 kernel already contains the matching bounded owner-retirement correction. The actual same-SDK comparison strongly supports this explanation for the failed old-kernel control, but the logs do not capture the private Worker counters or preparation phase, so the precise internal state of PID 3740 remains an inference.

No VM, native program, build, test, Git operation or new control protocol was executed for this investigation. Only this new findings directory was written. Full source pins and assertions are in `Analysis.actual.json`.

## Exact source and binary binding

| Profile | DLL identity | Compiler source identity |
|---|---|---|
| baseline x64 | `f552226e00c16c9df1ef6cb94081c69b37185c44741197d5a337a4ed70d1fdef` | `0bbed0bfd1b81a0c924ca3927cd4c53dfa2e916ac3aa5da43c51c2b70bba091f` |
| baseline x86 | `90a214ca2d416eda8e980cab7aacb80fffef777ace20c06179f41e584dae093b` | same baseline source |
| candidate x64 | `d888637d5dd1476dde16b8c5bb28172d7b8ec3495b69c725fa3572013748139a` | `26c5b73e4915bb4539e87b987cd8e952005f0fe41d987138d8d8e3b13eb25c95` |
| candidate x86 | `dc8c37178f4bbd9260edc973d61af876cf8460178eaf89fa2994fdffc94abc9a` | same candidate source |

The import manifests bind those DLLs to complete actual compiler snapshots. The baseline snapshot is archived at `F:/work/soui-issue96/verification/production-validation/0.5-kernel-cache-rebuild-2026-10-15/x64/build/kernel-source-before.json` (4,585,875 B, `ae3cbffa48ed9188eec0dedc6738de1dbc5bc0f70b82f5bae3ba371dfe1aa9a9`). The candidate snapshot is `C:/sr06/verification/production-validation/0.6.0-source2-kernel-2026-10-29/common-provenance/source/Source2.source-after.json` (4,505,919 B, `c698985f7d46d02984a7eec46d96c1ebeac5164b816b61b560d96b657c136809`). Eleven relevant source files were individually checked against each actual snapshot. Historical imported `SOURCE-MANIFEST.json` was not substituted for either compiler snapshot.

## The liveness cycle

All baseline paths below are relative to `F:/work/soui-issue96/third_party/sources/miniblink/`.

1. `third_party/WebKit/Source/core/workers/WorkerThread.cpp:113` acquires a tracked task's raw lease. The lease remains through its last raw Worker access at line 131. It is released by the actual wrapper destructor at lines 96–102, not simply by `run()` returning. `WorkerShutdownRecord.h:225,234` increments/decrements `m_active` accordingly.
2. `content/web_impl_win/WebTimerBase.cpp:315–329` removes a fired one-shot timer from the ready heap but retains it in `m_unusedTimersToDelete`. `fired()` at line 417 only runs the task; the timer destructor at lines 218–225 actually deletes it.
3. Baseline `content/web_impl_win/WebThreadImpl.cpp:665` destroys that completed list only at the next `schedulerTasks()` entry. After the last callback, it returns without doing this retirement. The worker loop at lines 257–269 skips the scheduler when the heap is empty and waits indefinitely. Even an already-signaled event does not help when waking still finds no heap work.
4. `WorkerMessagingProxy.cpp:427–438` refuses `retireBacking()` until `rawAccessQuiescent()` is true. That predicate at `WorkerShutdownRecord.h:261` requires `scopePrepared && !m_active`. A retained completed task therefore prevents the very native shutdown/join that would otherwise perform final timer destruction. Alternatively, multiple retained leases can prevent `WorkerThread::shutdown():401` from obtaining sole raw access before scope preparation. The existing raw evidence does not choose between those two internal positions.
5. There is no hidden escape in `shutdownBackingThread()`: `platform/WebThreadSupportingGC.cpp:44–55` only removes GC observers and detaches GC; `WebSchedulerImpl.cpp:40` has an empty `shutdown()`. Neither drains or stops the worker thread.

This is a source-proven reachable cycle, not a reason to release a raw lease early. Actual Task destruction and held-HANDLE native join must remain required.

## Existing candidate correction and SDK boundary

`0018-input-ui-deadline-index.patch` (59,711 B, `e01da602512a53b3cf5a200cb257b3f756f20ae1f6c08a46baba02ebda127421`) adds owner-side physical retirement. Current `C:/sr06/third_party/sources/miniblink/content/web_impl_win/WebThreadImpl.cpp:790` retires completed timers after callback/observer return; line 263 also services retirement-only worker wakes. Lines 621–635 remove ownership before destruction, retire at most 32 per normal call, and explicitly wake remaining work. These exact bytes also exist in the current editable `C:/sc07` source. Final shutdown still completely drains its retained set.

The old and current SDK `shutdown_miniblink_runtime()` function bodies are identical after line-ending normalization (4,691 B, `021be8ff3e8d9f61116c52f7e3a0870624aff15c714009fe54d28eee91e56e2c`). The SDK correctly retains a Pending shutdown generation until the kernel prepare handshake completes. Both old DLL architectures provide all 59 required core APIs. Absence of optional `wkeGetInputUiApi` is allowed; that engine-owner/main-thread hook cannot retire a private Worker thread's timer objects on its behalf. No safe generic SDK fallback is established by these findings.

## Actual result and minimum next action

The current 0.6 comparison kept the fixture EXE, Web, installed SDK and 77 other runtime members identical and changed only `node.dll`. Old x64 completed 32 browser cases, then Runtime shutdown timed out after 20,017 ms; it naturally exited 1 with Job active count 0 and no forced termination. Current candidate x64/x86 already naturally passed on unchanged inputs. Those passes are reused, not rerun. Baseline x86 is unrun; a whole four-arm success is false.

Keep the current candidate kernel pair and preserve the baseline as a failed negative control. No product source change is needed for this specific cycle. If legacy-kernel maintenance is separately required, backport the bounded owner-retirement mechanism into a newly identified build; never alter the reference baseline, extend the deadline, decrement live leases early or bypass the join fence.

Existing regression sources already contain the narrow assertions: `tests/native/scheduler_continuation/fixture.cpp:156` checks last-task retirement without another pulse, line 158 checks bounded32 plus remainder progress, and `tests/native/vendor_lifetime/worker_lifetime/fixture.cpp:761` requires retirement before deleting the worker so shutdown draining cannot mask idle retention. Their sources were read only; this investigation claims no new test result. No extra browser retry is recommended merely to make the historical negative control pass. Exact failed-process counter/stack attribution would require a separate targeted diagnostic if that stronger claim is needed.
