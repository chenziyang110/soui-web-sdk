"""Read only selected VM PIDs. Never emit or persist a raw command line."""
import ctypes
from ctypes import wintypes
import hashlib
import json
import pathlib
import sys


def require(value, message):
    if not value:
        raise RuntimeError(message)


def pin(row):
    p = pathlib.Path(row["path"])
    require(p.is_file() and not p.is_symlink(), "Pinned occupancy input unavailable")
    b = p.read_bytes()
    require(len(b) == row["bytes"] and hashlib.sha256(b).hexdigest() == row["sha256"],
            "Pinned occupancy input changed")


def main():
    require(sys.platform == "win32" and len(sys.argv) == 3, "Exact Windows occupancy invocation required")
    plan_bytes = pathlib.Path(sys.argv[1]).read_bytes()
    require(hashlib.sha256(plan_bytes).hexdigest() == sys.argv[2], "Occupancy plan changed")
    plan = json.loads(plan_bytes)
    require(plan["schema"] == "soui.one-win7-immutable-base-conversion-plan/v2", "Occupancy plan schema differs")
    require(pathlib.Path(sys.executable).resolve() == pathlib.Path(plan["tools"]["python"]["path"]).resolve(),
            "Pinned Python interpreter required")
    require(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode, "Isolated no-cache Python required")
    dependencies = plan["occupancyDependencies"]
    for row in dependencies:
        pin(row)
    pin(plan["tools"]["occupancyPython"])
    sys.path.insert(0, plan["psutilSitePackages"])
    import psutil
    require(psutil.__version__ == "7.2.2", "Pinned psutil version differs")
    expected_psutil = {str(pathlib.Path(row["path"]).resolve()).casefold()
                       for row in dependencies if "/psutil/" in row["path"].replace("\\", "/").lower()}
    imported = {str(pathlib.Path(module.__file__).resolve()).casefold()
                for name, module in tuple(sys.modules.items())
                if (name == "psutil" or name.startswith("psutil.")) and getattr(module, "__file__", None)}
    require(imported == expected_psutil, "Unexpected psutil runtime module closure")

    request = json.loads(sys.stdin.read(65537))
    require(request["schema"] == "soui.native-vm-pid-selection/v1", "Native process selection required")
    selected = request["selected"]
    require(isinstance(selected, list) and len(selected) <= 64, "Bounded native VM selection required")
    allowed = {"vmware-vmx.exe", "vmware.exe", "qemu-system-x86_64.exe", "qemu-system-i386.exe",
               "virtualboxvm.exe", "vboxheadless.exe"}
    require(len({row["pid"] for row in selected}) == len(selected), "Duplicate selected VM PID")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    kernel.GetProcessTimes.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL

    def birth(handle):
        values = [wintypes.FILETIME() for _ in range(4)]
        require(kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in values)),
                "Selected VM birth identity inaccessible")
        return str((values[0].dwHighDateTime << 32) | values[0].dwLowDateTime)

    rows = []
    for row in selected:
        require(isinstance(row["pid"], int) and row["pid"] > 0 and row["name"].lower() in allowed,
                "Only native-selected exact VM process names may be queried")
        handle = kernel.OpenProcess(0x1000, False, row["pid"])
        require(handle, "Selected VM query handle inaccessible")
        try:
            require(birth(handle) == row["creationFileTime"], "Selected VM PID/birth changed before query")
            process = psutil.Process(row["pid"])
            require(process.name().lower() == row["name"].lower(), "Selected VM process name changed")
            command = process.cmdline()
            require(command and all(isinstance(part, str) for part in command) and any(command),
                    "Selected VM command line unavailable")
            references_source = any("f:/vm/" in part.replace("\\", "/").lower() for part in command)
            del command
            require(birth(handle) == row["creationFileTime"] and process.name().lower() == row["name"].lower(),
                    "Selected VM identity changed during query")
            rows.append({"pid": row["pid"], "name": row["name"],
                         "creationFileTime": row["creationFileTime"], "commandLineAvailable": True,
                         "referencesExactFvmRoot": references_source})
        finally:
            require(kernel.CloseHandle(handle), "Selected VM identity handle release failed")
    for row in dependencies:
        pin(row)
    print(json.dumps({"schema": "soui.targeted-vm-commandline-observation/v1", "rows": rows,
                      "rawCommandLinesOmitted": True, "allNameEnumerationByPython": False,
                      "psutilVersion": psutil.__version__}, separators=(",", ":")))


try:
    main()
except BaseException as error:
    # Exception messages/arguments could include command lines; do not publish them.
    print(json.dumps({"schema": "soui.targeted-vm-commandline-failure/v1",
                      "errorType": type(error).__name__, "rawCommandLinesOmitted": True}), file=sys.stderr)
    sys.exit(2)
