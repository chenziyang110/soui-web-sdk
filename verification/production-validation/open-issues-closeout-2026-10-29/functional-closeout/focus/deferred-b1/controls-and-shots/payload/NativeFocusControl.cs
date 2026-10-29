// Output-only preparation. Compile and execute only in the explicitly named task VM.
// One real tray interaction; only its one focus call is deferred past callback return.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace SouiIssueFocusControl {
  public sealed class Probe {
    private const uint Callback = 0x8000 + 19, IconId = 19, TimerId = 1;
    private const uint CompleteFocus = 0x8000 + 20;
    private const uint DeadlineMs = 60000, Style = 0x00CF0000;
    private const string WindowClass = "SouiIndependentNativeFocusControl";
    private readonly Stopwatch clock = Stopwatch.StartNew();
    private readonly string runId = Guid.NewGuid().ToString("D");
    private readonly JavaScriptSerializer serializer = new JavaScriptSerializer();
    private StreamWriter log;
    private Native.WndProc callback;
    private IntPtr owner, icon, instance, menu, unattachedSubmenu;
    private bool registered, added, versionSet, tracking, attemptedMenu, focusCalled;
    private bool foregroundSatisfied, postNullSucceeded, menuDestroyed, focusSucceeded;
    private bool invalid, deadlineReached, cleanupConfirmed = true;
    private int exitCode = 2, foregroundCalls, focusCalls, sequence;
    private uint taskbarMessage;
    private int contextCallbackDepth, nullDispatches, completionDispatches;
    private long dequeueOrder, nullDispatchOrder;
    private bool callbackReturnBoundaryReached, completionPosted, completionPending, callbackUnwindConfirmed;
    private bool queuedDispatch; private uint queuedMessage; private IntPtr queuedWindow;

    private Dictionary<string, object> Fields(params object[] pairs) {
      var value = new Dictionary<string, object>();
      for (int index = 0; index < pairs.Length; index += 2) value.Add((string)pairs[index], pairs[index + 1]);
      return value;
    }
    private void Emit(string name, Dictionary<string, object> fields) {
      fields.Add("schema", "soui.native-tray-focus-control-event/v1");
      fields.Add("runId", runId); fields.Add("sequence", ++sequence);
      fields.Add("elapsedMs", clock.ElapsedMilliseconds); fields.Add("event", name);
      log.WriteLine(serializer.Serialize(fields)); log.Flush();
      if (name == "interactive.ready" || name == "run.finished") {
        Console.WriteLine(serializer.Serialize(fields)); Console.Out.Flush();
      }
    }
    private static string Hex(IntPtr value) { return "0x" + unchecked((ulong)value.ToInt64()).ToString("x"); }
    private Dictionary<string, object> Window(IntPtr window) {
      uint process;
      uint thread = Native.GetWindowThreadProcessId(window, out process);
      IntPtr root = Native.GetAncestor(window, 2);
      return Fields("hwnd", Hex(window), "isWindow", Native.IsWindow(window),
        "visible", Native.IsWindowVisible(window), "root", Hex(root), "parent", Hex(Native.GetParent(window)),
        "threadId", thread, "processId", process, "style", Hex(Native.GetWindowLongPtrW(window, -16)),
        "topLevel", window != IntPtr.Zero && root == window);
    }
    private Dictionary<string, object> ThreadInfo(uint thread) {
      var info = new Native.GUITHREADINFO(); info.cbSize = (uint)Marshal.SizeOf(typeof(Native.GUITHREADINFO));
      Native.SetLastError(0); bool ok = Native.GetGUIThreadInfo(thread, ref info); int error = Marshal.GetLastWin32Error();
      return Fields("requestedThreadId", thread, "ok", ok, "lastErrorAfterClear", error,
        "flags", info.flags, "active", Hex(info.hwndActive), "focus", Hex(info.hwndFocus),
        "capture", Hex(info.hwndCapture), "menuOwner", Hex(info.hwndMenuOwner),
        "moveSize", Hex(info.hwndMoveSize), "caret", Hex(info.hwndCaret));
    }
    private Dictionary<string, object> Snapshot() {
      IntPtr foreground = Native.GetForegroundWindow(); uint process;
      uint foregroundThread = Native.GetWindowThreadProcessId(foreground, out process);
      return Fields("owner", Window(owner), "foreground", Window(foreground),
        "currentThreadId", Native.GetCurrentThreadId(), "currentProcessId", Native.GetCurrentProcessId(),
        "ownerThread", ThreadInfo(Native.GetCurrentThreadId()),
        "foregroundThread", ThreadInfo(foregroundThread), "foregroundThreadId", foregroundThread,
        "foregroundWasNull", foreground == IntPtr.Zero);
    }
    private Native.NOTIFYICONDATAW Data() {
      var value = new Native.NOTIFYICONDATAW();
      value.cbSize = (uint)Marshal.SizeOf(typeof(Native.NOTIFYICONDATAW));
      value.hWnd = owner; value.uID = IconId;
      value.szTip = ""; value.szInfo = ""; value.szInfoTitle = "";
      return value;
    }
    private bool Notify(uint operation, string name, Native.NOTIFYICONDATAW data) {
      int size = Marshal.SizeOf(typeof(Native.NOTIFYICONDATAW));
      IntPtr pointer = Marshal.AllocHGlobal(size);
      try {
        // The SDK uses NOTIFYICONDATAW native{}. Preserve zero padding and all
        // unused members instead of depending on uninitialised marshal padding.
        var zero = new byte[size]; Marshal.Copy(zero, 0, pointer, size);
        Marshal.StructureToPtr(data, pointer, false);
        var bytes = new byte[size]; Marshal.Copy(pointer, bytes, 0, size);
        string digest;
        using (var sha = SHA256.Create()) digest = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
        Native.SetLastError(0);
        bool accepted = Native.Shell_NotifyIconW(operation, pointer);
        int error = Marshal.GetLastWin32Error(); // Must be the first diagnostic read.
        // Retain actual acquired resources even if evidence I/O itself fails.
        if (accepted && operation == 0) added = true;
        if (accepted && operation == 4) versionSet = true;
        if (accepted && operation == 2) added = false;
        Emit(name, Fields("operation", operation, "accepted", accepted, "lastErrorAfterClear", error,
          "extendedErrorContractDocumented", false, "cbSize", data.cbSize, "hWnd", Hex(data.hWnd),
          "uID", data.uID, "uFlags", data.uFlags, "callbackMessage", data.uCallbackMessage,
          "hIcon", Hex(data.hIcon), "versionOrTimeout", data.uTimeoutOrVersion,
          "guidItem", data.guidItem.ToString("D"), "hBalloonIcon", Hex(data.hBalloonIcon),
          "nativeBytesSha256", digest));
        return accepted;
      } finally { Marshal.FreeHGlobal(pointer); }
    }
    private void Invalidate(string reason) {
      if (!invalid) Emit("control.invalid", Fields("reason", reason, "snapshot", Snapshot()));
      invalid = true; exitCode = 2;
      if (tracking) {
        Native.SetLastError(0); bool ended = Native.EndMenu(); int error = Marshal.GetLastWin32Error();
        Emit("menu.abort", Fields("accepted", ended, "lastErrorAfterClear", error, "reason", reason));
        if (!ended) cleanupConfirmed = false;
      }
      Native.PostQuitMessage(exitCode);
    }
    private void Insert(IntPtr parent, uint index, uint id, string label, bool disabled, bool isChecked, bool separator, IntPtr submenu) {
      var item = new Native.MENUITEMINFOW(); item.cbSize = (uint)Marshal.SizeOf(typeof(Native.MENUITEMINFOW));
      item.fMask = 0x100 | 0x1; // MIIM_FTYPE | MIIM_STATE
      item.fType = separator ? 0x800U : 0U; item.fState = (disabled ? 3U : 0U) | (isChecked ? 8U : 0U);
      IntPtr text = IntPtr.Zero;
      try {
        if (!separator) { item.fMask |= 0x40; text = Marshal.StringToHGlobalUni(label); item.dwTypeData = text; item.cch = (uint)label.Length; }
        if (submenu != IntPtr.Zero) { item.fMask |= 0x4; item.hSubMenu = submenu; }
        else if (!separator) { item.fMask |= 0x2; item.wID = id; }
        Native.SetLastError(0); bool inserted = Native.InsertMenuItemW(parent, index, true, ref item); int error = Marshal.GetLastWin32Error();
        Emit("menu.insert", Fields("position", index, "id", id, "accepted", inserted, "lastErrorAfterClear", error,
          "disabled", disabled, "checked", isChecked, "separator", separator, "submenu", Hex(submenu)));
        if (!inserted) throw new InvalidOperationException("InsertMenuItemW rejected the exact control layout.");
      } finally { if (text != IntPtr.Zero) Marshal.FreeHGlobal(text); }
    }
    private void BuildMenu() {
      Native.SetLastError(0); menu = Native.CreatePopupMenu(); int error = Marshal.GetLastWin32Error();
      Emit("menu.create", Fields("handle", Hex(menu), "lastErrorAfterClear", error));
      if (menu == IntPtr.Zero) throw new InvalidOperationException("CreatePopupMenu failed.");
      Native.SetLastError(0); unattachedSubmenu = Native.CreatePopupMenu(); error = Marshal.GetLastWin32Error();
      Emit("submenu.create", Fields("handle", Hex(unattachedSubmenu), "lastErrorAfterClear", error));
      if (unattachedSubmenu == IntPtr.Zero) throw new InvalidOperationException("CreatePopupMenu submenu failed.");
      Insert(unattachedSubmenu, 0, 3, "\u7981\u7528\u9879", true, false, false, IntPtr.Zero);
      Insert(unattachedSubmenu, 1, 4, "\u5df2\u52fe\u9009\u9879", false, true, false, IntPtr.Zero);
      Insert(menu, 0, 1, "\u66f4\u65b0\u6258\u76d8\u63d0\u793a", false, false, false, IntPtr.Zero);
      Insert(menu, 1, 0, "\u516c\u5171 SDK \u83dc\u5355", false, false, false, unattachedSubmenu);
      unattachedSubmenu = IntPtr.Zero; // Root now owns its attached submenu.
      Insert(menu, 2, 0, "", false, false, true, IntPtr.Zero);
      Insert(menu, 3, 2, "\u79fb\u9664\u6258\u76d8\u5e76\u9000\u51fa", false, false, false, IntPtr.Zero);
    }
    private bool DestroyPopup() {
      if (menu == IntPtr.Zero) return menuDestroyed;
      Native.SetLastError(0); bool destroyed = Native.DestroyMenu(menu); int error = Marshal.GetLastWin32Error();
      Emit("menu.destroy", Fields("handle", Hex(menu), "accepted", destroyed, "lastErrorAfterClear", error));
      menuDestroyed = destroyed;
      if (destroyed) menu = IntPtr.Zero; else cleanupConfirmed = false;
      return destroyed;
    }
    private void ShowMenu() {
      if (attemptedMenu) { Invalidate("More than one context-menu request; no probe retry permitted."); return; }
      attemptedMenu = true;
      Emit("menu.requested", Fields("trustedGesture", false, "externalObservationRequired", true, "snapshot", Snapshot()));
      uint process; uint thread = Native.GetWindowThreadProcessId(owner, out process);
      if (!Native.IsWindow(owner) || Native.GetAncestor(owner, 2) != owner || Native.GetParent(owner) != IntPtr.Zero ||
          thread != Native.GetCurrentThreadId() || process != Native.GetCurrentProcessId()) {
        Invalidate("Owner identity or top-level precondition differs."); return;
      }
      BuildMenu();
      if (Native.GetForegroundWindow() != owner) {
        ++foregroundCalls;
        Native.SetLastError(0); bool accepted = Native.SetForegroundWindow(owner); int error = Marshal.GetLastWin32Error();
        Emit("foreground.attempt", Fields("accepted", accepted, "lastErrorAfterClear", error,
          "calls", foregroundCalls, "snapshot", Snapshot()));
      }
      foregroundSatisfied = Native.GetForegroundWindow() == owner;
      if (!foregroundSatisfied) { Invalidate("Hidden owner was not foreground after at most one ordinary attempt."); return; }
      Native.POINT position;
      Native.SetLastError(0); bool cursor = Native.GetCursorPos(out position); int cursorError = Marshal.GetLastWin32Error();
      if (!cursor) { Emit("cursor.failed", Fields("lastErrorAfterClear", cursorError)); Invalidate("GetCursorPos failed."); return; }
      Emit("menu.tracking", Fields("flags", 0x182, "x", position.x, "y", position.y, "snapshot", Snapshot()));
      tracking = true;
      uint command; int menuError;
      try {
        Native.SetLastError(0); command = Native.TrackPopupMenuEx(menu, 0x182, position.x, position.y, owner, IntPtr.Zero);
        menuError = Marshal.GetLastWin32Error();
      } finally { tracking = false; }
      Emit("menu.returned", Fields("command", command, "lastErrorAfterClear", menuError,
        "noSelection", command == 0, "escapeEstablishedByController", false, "snapshot", Snapshot()));
      Native.SetLastError(0); postNullSucceeded = Native.PostMessageW(owner, 0, UIntPtr.Zero, IntPtr.Zero); int postError = Marshal.GetLastWin32Error();
      Emit("menu.post-null", Fields("accepted", postNullSucceeded, "lastErrorAfterClear", postError,
        "contextCallbackDepth", contextCallbackDepth, "inSendMessageEx", Native.InSendMessageEx(IntPtr.Zero)));
      bool destroyed = DestroyPopup();
      if (invalid || deadlineReached || clock.ElapsedMilliseconds >= DeadlineMs) { Invalidate("Deadline/abort occurred before focus; no focus call made."); return; }
      if (command != 0 || menuError != 0 || !postNullSucceeded || !destroyed) { Invalidate("Expected unselected/error-free menu return and exact finish/cleanup were not confirmed."); return; }
      completionPending = true;
      Native.SetLastError(0); completionPosted = Native.PostMessageW(owner, CompleteFocus, UIntPtr.Zero, IntPtr.Zero); int completionError = Marshal.GetLastWin32Error();
      Emit("focus.completion.posted", Fields("accepted", completionPosted, "lastErrorAfterClear", completionError,
        "message", CompleteFocus, "contextCallbackDepth", contextCallbackDepth,
        "inSendMessageEx", Native.InSendMessageEx(IntPtr.Zero)));
      if (!completionPosted) Invalidate("The single deferred focus completion could not be posted.");
    }
    private void CompleteDeferredFocus(IntPtr window, UIntPtr first, IntPtr second) {
      ++completionDispatches;
      uint sent = Native.InSendMessageEx(IntPtr.Zero);
      Emit("focus.completion.dispatched", Fields("dispatches", completionDispatches, "dequeueOrder", dequeueOrder,
        "nullDispatches", nullDispatches, "nullDispatchOrder", nullDispatchOrder,
        "contextCallbackDepth", contextCallbackDepth, "callbackReturnBoundaryReached", callbackReturnBoundaryReached,
        "queuedDispatch", queuedDispatch, "inSendMessageEx", sent));
      if (!completionPosted || !completionPending || completionDispatches != 1 || focusCalled || tracking ||
          window != owner || first != UIntPtr.Zero || second != IntPtr.Zero || !queuedDispatch ||
          queuedWindow != owner || queuedMessage != CompleteFocus || sent != 0 || contextCallbackDepth != 0 ||
          !callbackReturnBoundaryReached || nullDispatches != 1 || nullDispatchOrder <= 0 || nullDispatchOrder >= dequeueOrder) {
        Invalidate("Deferred completion did not follow one queued WM_NULL after the original tray callback returned."); return;
      }
      completionPending = false;
      if (invalid || deadlineReached || clock.ElapsedMilliseconds >= DeadlineMs) { Invalidate("Original deadline/abort occurred before deferred focus; no focus call made."); return; }
      uint process; uint thread = Native.GetWindowThreadProcessId(owner, out process);
      if (!added || !versionSet || !postNullSucceeded || !menuDestroyed || menu != IntPtr.Zero ||
          !Native.IsWindow(owner) || Native.GetAncestor(owner, 2) != owner || Native.GetParent(owner) != IntPtr.Zero ||
          thread != Native.GetCurrentThreadId() || process != Native.GetCurrentProcessId() || Native.GetForegroundWindow() != owner) {
        Invalidate("Original owner/foreground/menu/registration preconditions changed before deferred focus; no retry permitted."); return;
      }
      callbackUnwindConfirmed = true;
      Emit("focus.callback-unwind.confirmed", Fields("dequeueOrder", dequeueOrder, "nullDispatchOrder", nullDispatchOrder,
        "contextCallbackDepth", contextCallbackDepth, "inSendMessageEx", sent));
      Emit("focus.before", Fields("snapshot", Snapshot(), "inSendMessageEx", Native.InSendMessageEx(IntPtr.Zero)));
      if (focusCalled) throw new InvalidOperationException("Focus call duplication refused.");
      focusCalled = true; ++focusCalls;
      focusSucceeded = Notify(3, "tray.focus", Data());
      Emit("focus.after", Fields("snapshot", Snapshot(), "accepted", focusSucceeded, "calls", focusCalls,
        "inSendMessageEx", Native.InSendMessageEx(IntPtr.Zero)));
      exitCode = focusSucceeded ? 0 : 1; // FALSE remains failure, including error=0.
      Native.PostQuitMessage(exitCode);
    }
    private IntPtr Dispatch(IntPtr window, uint message, UIntPtr first, IntPtr second) {
      try {
        if (window == owner && message == 0 && postNullSucceeded && !focusCalled && !invalid) {
          ++nullDispatches; uint sent = Native.InSendMessageEx(IntPtr.Zero);
          Emit("menu.null.dispatched", Fields("dispatches", nullDispatches, "dequeueOrder", dequeueOrder,
            "contextCallbackDepth", contextCallbackDepth, "callbackReturnBoundaryReached", callbackReturnBoundaryReached,
            "queuedDispatch", queuedDispatch, "inSendMessageEx", sent));
          if (nullDispatches != 1 || !queuedDispatch || queuedWindow != owner || queuedMessage != 0 || sent != 0 ||
              first != UIntPtr.Zero || second != IntPtr.Zero || contextCallbackDepth != 0 || !callbackReturnBoundaryReached) {
            Invalidate("Expected one queued WM_NULL after callback return was not observed."); return IntPtr.Zero;
          }
          nullDispatchOrder = dequeueOrder;
          return IntPtr.Zero;
        }
        if (message == CompleteFocus) { CompleteDeferredFocus(window, first, second); return IntPtr.Zero; }
        if (taskbarMessage != 0 && message == taskbarMessage) { Invalidate("TaskbarCreated occurred; control does not restart Shell/reinstall/retry."); return IntPtr.Zero; }
        if (message == 0x10) { Invalidate("Owner closed before control completed."); return IntPtr.Zero; }
        if (message == 0x113 && first.ToUInt64() == TimerId && clock.ElapsedMilliseconds >= DeadlineMs) {
          deadlineReached = true; Invalidate("Fixed60000ms diagnostic deadline expired."); return IntPtr.Zero;
        }
        if (message == Callback && added && versionSet) {
          uint packed = unchecked((uint)second.ToInt64()); uint id = packed >> 16; uint notification = packed & 0xffff;
          if (id == IconId && notification == 0x7b) {
            ++contextCallbackDepth;
            try {
              Emit("tray.context-menu", Fields("iconId", id, "notification", notification,
                "coordinatesDefinedForNotification", false, "trustedGesture", false,
                "contextCallbackDepth", contextCallbackDepth, "inSendMessageEx", Native.InSendMessageEx(IntPtr.Zero)));
              if (!tracking && !focusCalled && !invalid) ShowMenu();
              else if (!focusCalled && !invalid) Invalidate("Nested context menu invalidates single-interaction control.");
            } finally {
              --contextCallbackDepth;
              callbackReturnBoundaryReached = contextCallbackDepth == 0 && completionPosted;
              Emit("tray.callback.returning", Fields("contextCallbackDepth", contextCallbackDepth,
                "completionPosted", completionPosted, "callbackReturnBoundaryReached", callbackReturnBoundaryReached,
                "inSendMessageEx", Native.InSendMessageEx(IntPtr.Zero), "actualUnwindNotYetClaimed", true));
            }
          }
          return IntPtr.Zero;
        }
        return Native.DefWindowProcW(window, message, first, second);
      } catch (Exception error) {
        try { Emit("dispatch.error", Fields("type", error.GetType().FullName, "message", error.Message)); Invalidate("Native callback failed."); }
        catch { invalid = true; exitCode = 2; if (tracking) Native.EndMenu(); Native.PostQuitMessage(2); }
        return IntPtr.Zero;
      }
    }
    private void Cleanup() {
      if (owner != IntPtr.Zero) {
        bool killed = Native.KillTimer(owner, new UIntPtr(TimerId));
        Emit("timer.cleanup", Fields("accepted", killed));
      }
      if (menu != IntPtr.Zero) DestroyPopup();
      if (unattachedSubmenu != IntPtr.Zero) {
        Native.SetLastError(0); bool destroyed = Native.DestroyMenu(unattachedSubmenu); int error = Marshal.GetLastWin32Error();
        Emit("submenu.cleanup", Fields("accepted", destroyed, "lastErrorAfterClear", error));
        if (!destroyed) cleanupConfirmed = false; else unattachedSubmenu = IntPtr.Zero;
      }
      if (added) {
        bool removed = Notify(2, "tray.delete", Data());
        if (!removed) cleanupConfirmed = false; else added = false;
      }
      if (icon != IntPtr.Zero) {
        Native.SetLastError(0); bool destroyed = Native.DestroyIcon(icon); int error = Marshal.GetLastWin32Error();
        Emit("icon.cleanup", Fields("accepted", destroyed, "lastErrorAfterClear", error));
        if (!destroyed) cleanupConfirmed = false; else icon = IntPtr.Zero;
      }
      if (owner != IntPtr.Zero) {
        Native.SetLastError(0); bool destroyed = Native.DestroyWindow(owner); int error = Marshal.GetLastWin32Error();
        Emit("owner.cleanup", Fields("accepted", destroyed, "lastErrorAfterClear", error));
        if (!destroyed || Native.IsWindow(owner)) cleanupConfirmed = false; else owner = IntPtr.Zero;
      }
      if (registered) {
        Native.SetLastError(0); bool removed = Native.UnregisterClassW(WindowClass, instance); int error = Marshal.GetLastWin32Error();
        Emit("class.cleanup", Fields("accepted", removed, "lastErrorAfterClear", error));
        if (!removed) cleanupConfirmed = false; else registered = false;
      }
      if (!cleanupConfirmed) exitCode = 2;
    }
    private void VerifyLayout() {
      if (IntPtr.Size != 8 || Marshal.SizeOf(typeof(Native.NOTIFYICONDATAW)) != 976 || Marshal.SizeOf(typeof(Native.GUITHREADINFO)) != 72 ||
          Marshal.SizeOf(typeof(Native.WNDCLASSW)) != 72 || Marshal.SizeOf(typeof(Native.MENUITEMINFOW)) != 80 || Marshal.SizeOf(typeof(Native.MSG)) != 48)
        throw new InvalidOperationException("Unexpected full x64 Windows structure layout.");
      string[] fields = {"cbSize", "hWnd", "uID", "uFlags", "uCallbackMessage", "hIcon", "szTip", "dwState", "dwStateMask", "szInfo", "uTimeoutOrVersion", "szInfoTitle", "dwInfoFlags", "guidItem", "hBalloonIcon"};
      int[] expected = {0, 8, 16, 20, 24, 32, 40, 296, 300, 304, 816, 820, 948, 952, 968};
      var observed = new Dictionary<string, object>();
      for (int index = 0; index < fields.Length; ++index) {
        long offset = Marshal.OffsetOf(typeof(Native.NOTIFYICONDATAW), fields[index]).ToInt64();
        observed.Add(fields[index], offset); if (offset != expected[index]) throw new InvalidOperationException("NOTIFYICONDATAW offset mismatch: " + fields[index]);
      }
      Emit("layout.verified", Fields("notifyIconBytes", 976, "offsets", observed));
    }
    public static int Run(string outputDirectory, string machine, string gate) {
      if (Environment.OSVersion.Platform != PlatformID.Win32NT || IntPtr.Size != 8 ||
          !String.Equals(Environment.MachineName, machine, StringComparison.OrdinalIgnoreCase) ||
          !String.Equals(machine, "SOUI-7AEMMK5Q0T", StringComparison.OrdinalIgnoreCase) ||
          gate != "ROOT_REVIEWED_NATIVE_FOCUS_CONTROL_V1" || !Directory.Exists(outputDirectory))
        throw new InvalidOperationException("Reviewed named task VM only; host execution forbidden.");
      var probe = new Probe(); return probe.Execute(outputDirectory);
    }
    private int Execute(string outputDirectory) {
      log = new StreamWriter(new FileStream(Path.Combine(outputDirectory, "Native.events.ndjson"), FileMode.CreateNew, FileAccess.Write, FileShare.Read), new UTF8Encoding(false));
      try {
        var process = Process.GetCurrentProcess();
        Emit("run.started", Fields("machine", Environment.MachineName, "architecture", "x64", "pid", process.Id,
          "creationFileTime", process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString(), "timeoutMs", DeadlineMs,
          "source", "direct-Win32-control-no-SDK", "sdkExecuted", false, "trustedGesture", false));
        VerifyLayout(); callback = Dispatch; instance = Native.GetModuleHandleW(null);
        taskbarMessage = Native.RegisterWindowMessageW("TaskbarCreated");
        if (taskbarMessage < 0xc000 || taskbarMessage > 0xffff) throw new InvalidOperationException("Cannot register TaskbarCreated.");
        var description = new Native.WNDCLASSW(); description.lpfnWndProc = callback; description.hInstance = instance; description.lpszClassName = WindowClass;
        Native.SetLastError(0); ushort atom = Native.RegisterClassW(ref description); int classError = Marshal.GetLastWin32Error();
        registered = atom != 0;
        Emit("class.register", Fields("atom", atom, "lastErrorAfterClear", classError));
        if (!registered) throw new InvalidOperationException("RegisterClassW failed.");
        owner = Native.CreateWindowExW(0, WindowClass, "SOUI native tray focus control", Style, unchecked((int)0x80000000), unchecked((int)0x80000000), 360, 200, IntPtr.Zero, IntPtr.Zero, instance, IntPtr.Zero);
        if (owner == IntPtr.Zero) throw new InvalidOperationException("CreateWindowExW failed.");
        if (Native.IsWindowVisible(owner)) throw new InvalidOperationException("Owner must remain hidden like the independent tray consumer.");
        Emit("owner.created", Fields("snapshot", Snapshot()));
        IntPtr shared = Native.LoadIconW(IntPtr.Zero, new IntPtr(32516)); // IDI_INFORMATION
        if (shared == IntPtr.Zero) throw new InvalidOperationException("LoadIconW failed.");
        icon = Native.CopyIcon(shared); if (icon == IntPtr.Zero) throw new InvalidOperationException("CopyIcon failed.");
        var data = Data(); data.uFlags = 0x87; data.uCallbackMessage = Callback; data.hIcon = icon;
        data.szTip = "SOUI SDK \u72ec\u7acb\u6258\u76d8\u6d88\u8d39\u8005";
        added = Notify(0, "tray.add", data); if (!added) throw new InvalidOperationException("NIM_ADD failed.");
        data = Data(); data.uTimeoutOrVersion = 4; versionSet = Notify(4, "tray.version", data);
        if (!versionSet) throw new InvalidOperationException("NIM_SETVERSION4 failed.");
        if (Native.SetTimer(owner, new UIntPtr(TimerId), 100, IntPtr.Zero) == UIntPtr.Zero) throw new InvalidOperationException("SetTimer failed.");
        Emit("interactive.ready", Fields("instructions", "Right-click this control's tray icon once; when the menu is visible press Escape once. Do not select an item. Root must retain external gesture evidence.", "timeoutMs", DeadlineMs));
        Native.MSG message; int received;
        while ((received = Native.GetMessageW(out message, IntPtr.Zero, 0, 0)) > 0) {
          ++dequeueOrder; queuedDispatch = true; queuedMessage = message.message; queuedWindow = message.hwnd;
          if (message.hwnd == owner && (message.message == CompleteFocus || (message.message == 0 && postNullSucceeded)))
            Emit("queue.dequeued", Fields("message", message.message, "dequeueOrder", dequeueOrder,
              "contextCallbackDepth", contextCallbackDepth, "callbackReturnBoundaryReached", callbackReturnBoundaryReached,
              "inSendMessageEx", Native.InSendMessageEx(IntPtr.Zero)));
          try { Native.TranslateMessage(ref message); Native.DispatchMessageW(ref message); }
          finally { queuedDispatch = false; queuedMessage = 0; queuedWindow = IntPtr.Zero; }
        }
        if (received < 0) Invalidate("GetMessageW failed.");
        if (!focusCalled && !invalid) Invalidate("Message loop ended without the single focus observation.");
      } catch (Exception error) {
        invalid = true; exitCode = 2;
        Emit("run.error", Fields("type", error.GetType().FullName, "message", error.Message));
      } finally {
        try { Cleanup(); } catch (Exception error) { cleanupConfirmed = false; invalid = true; exitCode = 2; Emit("cleanup.error", Fields("type", error.GetType().FullName, "message", error.Message)); }
        bool valid = !invalid && !deadlineReached && foregroundSatisfied && postNullSucceeded && menuDestroyed && focusCalls == 1 &&
          completionPosted && !completionPending && completionDispatches == 1 && nullDispatches == 1 && callbackUnwindConfirmed;
        bool accepted = valid && focusSucceeded && cleanupConfirmed && exitCode == 0;
        Emit("run.finished", Fields("exitCode", exitCode, "controlValid", valid, "focusAccepted", focusSucceeded,
          "controlSucceeded", accepted, "foregroundCalls", foregroundCalls, "focusCalls", focusCalls,
          "cleanupConfirmed", cleanupConfirmed, "deadlineReached", deadlineReached,
          "deferredCompletionPosted", completionPosted, "completionDispatches", completionDispatches,
          "nullDispatches", nullDispatches, "callbackUnwindConfirmed", callbackUnwindConfirmed,
          "escapeEstablishedByController", false, "externalGestureEvidenceRequired", true,
          "sdkFixed", false, "x86Qualified", false, "productionQualificationComplete", false, "releaseReady", false));
        log.Dispose(); GC.KeepAlive(callback);
      }
      return exitCode;
    }
  }

  internal static class Native {
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] public delegate IntPtr WndProc(IntPtr window, uint message, UIntPtr first, IntPtr second);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO {
      public uint cbSize, flags; public IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret; public RECT rcCaret;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] public struct NOTIFYICONDATAW {
      public uint cbSize; public IntPtr hWnd; public uint uID, uFlags, uCallbackMessage; public IntPtr hIcon;
      [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string szTip;
      public uint dwState, dwStateMask;
      [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szInfo;
      public uint uTimeoutOrVersion;
      [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string szInfoTitle;
      public uint dwInfoFlags; public Guid guidItem; public IntPtr hBalloonIcon;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] public struct WNDCLASSW {
      public uint style; public WndProc lpfnWndProc; public int cbClsExtra, cbWndExtra; public IntPtr hInstance, hIcon, hCursor, hbrBackground;
      [MarshalAs(UnmanagedType.LPWStr)] public string lpszMenuName;
      [MarshalAs(UnmanagedType.LPWStr)] public string lpszClassName;
    }
    [StructLayout(LayoutKind.Sequential)] public struct MENUITEMINFOW {
      public uint cbSize, fMask, fType, fState, wID; public IntPtr hSubMenu, hbmpChecked, hbmpUnchecked; public UIntPtr dwItemData;
      public IntPtr dwTypeData; public uint cch; public IntPtr hbmpItem;
    }
    [StructLayout(LayoutKind.Sequential)] public struct MSG {
      public IntPtr hwnd; public uint message; public UIntPtr wParam; public IntPtr lParam; public uint time; public POINT pt; public uint lPrivate;
    }
    [DllImport("kernel32.dll", ExactSpelling = true)] public static extern void SetLastError(uint error);
    [DllImport("kernel32.dll", ExactSpelling = true)] public static extern uint GetCurrentThreadId();
    [DllImport("kernel32.dll", ExactSpelling = true)] public static extern uint GetCurrentProcessId();
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] public static extern IntPtr GetModuleHandleW(string module);
    [DllImport("shell32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool Shell_NotifyIconW(uint operation, IntPtr data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)] public static extern ushort RegisterClassW(ref WNDCLASSW description);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool UnregisterClassW(string name, IntPtr instance);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)] public static extern IntPtr CreateWindowExW(uint extended, string className, string title, uint style, int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr parameter);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool DestroyWindow(IntPtr window);
    [DllImport("user32.dll", ExactSpelling = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll", ExactSpelling = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll", ExactSpelling = true)] public static extern IntPtr GetAncestor(IntPtr window, uint flags);
    [DllImport("user32.dll", ExactSpelling = true)] public static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll", ExactSpelling = true)] public static extern IntPtr GetWindowLongPtrW(IntPtr window, int index);
    [DllImport("user32.dll", ExactSpelling = true)] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", ExactSpelling = true)] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", ExactSpelling = true)] public static extern uint InSendMessageEx(IntPtr reserved);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool GetGUIThreadInfo(uint thread, ref GUITHREADINFO info);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] public static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool InsertMenuItemW(IntPtr menu, uint position, [MarshalAs(UnmanagedType.Bool)] bool byPosition, ref MENUITEMINFOW item);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] public static extern uint TrackPopupMenuEx(IntPtr menu, uint flags, int x, int y, IntPtr owner, IntPtr parameters);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool DestroyMenu(IntPtr menu);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool EndMenu();
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr first, IntPtr second);
    [DllImport("user32.dll", ExactSpelling = true)] public static extern void PostQuitMessage(int exitCode);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] public static extern UIntPtr SetTimer(IntPtr window, UIntPtr id, uint period, IntPtr procedure);
    [DllImport("user32.dll", ExactSpelling = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool KillTimer(IntPtr window, UIntPtr id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)] public static extern int GetMessageW(out MSG message, IntPtr window, uint first, uint last);
    [DllImport("user32.dll", ExactSpelling = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool TranslateMessage(ref MSG message);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] public static extern IntPtr DispatchMessageW(ref MSG message);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] public static extern IntPtr DefWindowProcW(IntPtr window, uint message, UIntPtr first, IntPtr second);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] public static extern IntPtr LoadIconW(IntPtr instance, IntPtr name);
    [DllImport("user32.dll", ExactSpelling = true)] public static extern IntPtr CopyIcon(IntPtr icon);
    [DllImport("user32.dll", ExactSpelling = true, SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] public static extern bool DestroyIcon(IntPtr icon);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] public static extern uint RegisterWindowMessageW(string message);
  }
}
