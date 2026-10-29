# Direct native tray focus control — prepared, not executed

This output-only control narrows the actual `E_TRAY_FOCUS` observations without changing the SDK, the existing x86/x64 tray EXEs, Shell settings or their original results. It is authored preparation, not an independent review, compile, test or issue closure.

The only supported execution is one x64 Windows PowerShell process in the existing task VM `SOUI-7AEMMK5Q0T`. Root first independently reviews these exact files and their preparation SHA, retains the VM lease and checks no other task inputs that desktop. There is no host execution or host MSVC step. PowerShell Add-Type compiles only this C# direct Win32 control inside the guest. No SDK, kernel or Web artifact is rebuilt.

Copy the four flat files to a fresh guest folder such as `C:\focusc1`. Use the noVNC GUI to type a reviewed command, substituting the actual preparation digest and a new evidence suffix:

```powershell
C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -STA -File C:\focusc1\Invoke-NativeFocusControl.ps1 -ExpectedMachine SOUI-7AEMMK5Q0T -EvidenceDirectory C:\issue-closeout-evidence\focusc-ONE-FRESH-ID -PreparationSha256 ACTUAL_SHA256 -RootGate ROOT_REVIEWED_NATIVE_FOCUS_CONTROL_V1
```

Use only lowercase letters/digits/hyphens in the evidence suffix. A preparation hash mismatch, wrong machine/process architecture, missing STA, prior evidence directory or busy desktop mutex stops before the control. Root may wrap the exact guest command in the existing natural-only OwnedProcessRunner for separate whole-process/Job closure proof; this script itself proves only its explicit native-resource cleanup and returned process code.

When `interactive.ready` appears, right-click this control's information tray icon once. Its tooltip matches the old consumer (`SOUI SDK 独立托盘消费者`). Retain a screenshot of the visible menu, then press Escape once. The 60-second diagnostic deadline begins when the C# control starts, does not renew and does not modify the old consumer's deadlines. No menu is opened by timer/hotkey/synthetic message. A normal timer only bounds the observation; if it must EndMenu, the control is invalid and no focus call is made.

The owner is the same hidden top-level `WS_OVERLAPPEDWINDOW`, 360×200, icon ID19 and callback `WM_APP+19`. The control uses a copied `IDI_INFORMATION` icon, fresh full Unicode `NOTIFYICONDATAW` with checked x64 layout976 and zero padding, add flags0x87, then version4. ADD and SETVERSION must both succeed. Callback input is not authenticated: the controller always records `trustedGesture=false`; Root's actual noVNC steps and screenshots provide the external interaction evidence.

After the real context-menu callback, the control uses the same menu layout, at most one ordinary SetForegroundWindow, verifies actual foreground identity, then TrackPopupMenuEx flags0x182 (`TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON`), posts WM_NULL and destroys the root menu. No AttachThreadInput, ShowWindow, SetWindowPos, shell restart, credential change or retry is present. An unselected zero/error0 return is recorded as no selection; it does not establish Escape without the external observation. A selected command, foreground denial, changed Shell, timeout or failed menu finish invalidates this control and skips focus.

For a valid control there is exactly one NIM_SETFOCUS with a fresh structure containing only cbSize/hWnd/uID, matching SDK behavior. SetLastError(0), the native BOOL, and Marshal.GetLastWin32Error are adjacent. FALSE is failure even if the recorded diagnostic is0. Microsoft does not document a GetLastError contract for this API, so the error is retained only as a diagnostic. Before/after snapshots include owner and foreground HWND/root/parent/style/visibility/PID/TID and GetGUIThreadInfo for their threads. A changing foreground can make that diagnostic snapshot inconclusive.

The control returns0 only for the one accepted native focus operation with valid preconditions and explicit successful cleanup;1 for a valid native FALSE;2 for invalid setup, timeout, exception or cleanup failure. All events remain in `Native.events.ndjson` and PowerShell keeps compile/invocation/exit/environment/mutex records. It never changes `releaseReady`, `productionQualificationComplete` or `sdkFixed` totrue. One native TRUE narrows SDK parameter/owner/timing differences; one native FALSE narrows the environment/Shell path. Neither result alone establishes an SDK defect or fix, x86 qualification, application keyboard focus semantics, long residency or production readiness. Keep existing failing samples unchanged; do not mechanically rerun them.

Primary documentation consulted on2026-10-29:

- [Shell_NotifyIconW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw): operation constants, identity/version handling and native BOOL contract.
- [NOTIFYICONDATAW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataw): full Unicode fields and version4 callback packing.
- [TrackPopupMenu](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenu) and [TrackPopupMenuEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenuex): foreground requirement, no-selection ambiguity, flags and post-menu WM_NULL.
- [SetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow): ordinary foreground restrictions; the control preserves denial.
- [GetGUIThreadInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo) and [GUITHREADINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-guithreadinfo): thread-specific observations, cbSize and transient foreground limitations.

The current local Windows SDK10.0.19041.0 `um/shellapi.h` supplied the matching native field order/constants. Preparation records its bytes and the exact current SDK tray/menu/consumer source identities; those are navigation/source references, not a runtime result.
