#!/usr/bin/env python3
"""Local QEMU/noVNC lifecycle. No guest input automation or run deletion."""
import argparse
from contextlib import contextmanager, ExitStack
import hashlib
import http.server
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
import uuid

sys.dont_write_bytecode = True
import lab_common as common
from lab_common import read_json, write_json, file_path, command, image_chain, free_ports, QMP

ASSETS = common.ASSETS
WINDOWS_FLAGS = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
OFFLINE_METADATA = ('lab.json', 'started.json', 'active-session.json', 'start.lock')
SESSION_FILES = ('started.json', 'stopped.json', 'failed.json', 'start.lock',
                 'active-session.json', 'qemu.stdout.log', 'qemu.stderr.log', 'serial.log',
                 'offline-recovered.json')


def validate_config(config):
    return common.validate_config(config, common.GENERIC_PROFILE)


def qemu_argv(meta, run):
    c, p = meta['config'], meta['ports']
    args = [c['qemu'], '-name', 'novnc-lab-' + meta['uuid'], '-uuid', meta['uuid'],
            '-machine', 'pc', '-accel', c['accel'], '-cpu', c.get('cpu', 'max'),
            '-m', str(c['memory_mb']), '-smp', str(c['cpus']),
            '-display', 'none', '-vga', 'std', '-boot', 'order=c',
            '-qmp', 'tcp:127.0.0.1:%s,server=on,wait=off' % p['qmp'],
            '-vnc', '127.0.0.1:%s,websocket=127.0.0.1:%s' % (p['vnc'] - 5900, p['websocket']),
            '-chardev', 'file,id=seriallog,path=' + str(run / 'serial.log').replace(',', ',,'),
            '-serial', 'chardev:seriallog', '-net', 'none',
            '-device', 'usb-ehci,id=ehci', '-device', 'usb-tablet,bus=ehci.0']
    # Preserve explicit legacy BIOS/data settings when adopting an existing VM.
    if c.get('qemu_data'):
        data = Path(c['qemu_data']).expanduser().resolve(strict=True)
        if not data.is_dir():
            raise ValueError('qemu_data must be a directory')
        args += ['-L', str(data)]
    if c.get('bios'):
        if c.get('firmware_code') or c.get('firmware_vars'):
            raise ValueError('Explicit BIOS cannot be combined with UEFI firmware')
        args += ['-bios', str(file_path(c['bios']))]
    args += common.disk_stack(meta['parents'], run / 'overlay.qcow2', '', 'overlay')
    args += ['-device', 'ide-hd,drive=overlay,bus=ide.0,unit=0,id=systemdisk']
    if c['network'] == 'user':
        args += ['-netdev', 'user,id=guestnet', '-device', 'e1000,netdev=guestnet']
    if c['iso']:
        cd = {'driver': 'raw', 'node-name': 'cd', 'read-only': True,
              'file': {'driver': 'file', 'filename': c['iso'], 'read-only': True}}
        args += ['-blockdev', json.dumps(cd), '-device', 'ide-cd,drive=cd,bus=ide.1,unit=0,id=installcd']
    else:
        args += ['-device', 'ide-cd,bus=ide.1,unit=0,id=installcd']
    if c['firmware_code']:
        for i, path in enumerate((c['firmware_code'], str(run / 'firmware-vars.fd'))):
            args += ['-drive', 'if=pflash,format=raw,unit=%d,readonly=%s,file=%s' %
                     (i, 'on' if i == 0 else 'off', path.replace(',', ',,'))]
    return args


LabHTTP = common.LabHTTP


def prepare(path):
    config = validate_config(read_json(path))
    parents = image_chain(config)
    identity = str(uuid.uuid4())
    run = Path(config['runs_dir']) / identity
    run.mkdir(parents=True, exist_ok=False)
    ports = dict(zip(('http', 'websocket', 'qmp', 'vnc'), free_ports()))
    if ports['vnc'] < 5900:
        raise RuntimeError('Ephemeral VNC port below 5900; create a new run')
    command([config['qemu_img'], 'create', '-f', 'qcow2', '-F', parents[0]['format'],
             '-b', parents[0]['path'], str(run / 'overlay.qcow2')])
    if config['firmware_vars']:
        shutil.copyfile(config['firmware_vars'], run / 'firmware-vars.fd')
    meta = {'schema': 1, 'profile': common.GENERIC_PROFILE, 'uuid': identity, 'config': config, 'parents': parents,
            'ports': ports, 'url': 'http://127.0.0.1:%s/' % ports['http']}
    meta['argv'] = qemu_argv(meta, run)
    write_json(run / 'lab.json', meta)
    return {'run': str(run), 'url': meta['url'], 'state': 'prepared'}


def load_run(path):
    return common.load_run(path, common.GENERIC_PROFILE)


def _windows_process_exists(pid):
    """Read process liveness; os.kill on Windows can terminate its target."""
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
    kernel.GetExitCodeProcess.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        code = ctypes.get_last_error()
        if code == 87:  # ERROR_INVALID_PARAMETER: PID no longer exists.
            return False
        if code == 5:  # Access denied is not evidence of an exited process.
            return True
        raise ctypes.WinError(code)
    try:
        code = wintypes.DWORD()
        if not kernel.GetExitCodeProcess(handle, ctypes.byref(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        return code.value == 259  # STILL_ACTIVE
    finally:
        kernel.CloseHandle(handle)


def process_exists(pid):
    if not isinstance(pid, int) or isinstance(pid, bool) or pid <= 0:
        return False
    if os.name == 'nt':
        return _windows_process_exists(pid)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def vm_state(run, meta):
    """A matching live QMP endpoint takes precedence over historical receipts."""
    result = {'run': str(run), 'url': meta['url'], 'network': meta['config']['network'],
              'guest_ui_verified': False}
    try:
        qmp = QMP(meta)
    except (OSError, ConnectionError):
        qmp = None
    if qmp is not None:
        try:
            return dict(result, state='running', infrastructure=qmp.call('query-status'))
        finally:
            qmp.close()
    started = read_json(run / 'started.json') if (run / 'started.json').exists() else {}
    pids = [started.get('qemu_pid'), started.get('supervisor_pid')]
    if any(pid != os.getpid() and process_exists(pid) for pid in pids):
        return dict(result, state='unreachable')
    if (run / 'offline-recovery.pending.json').exists():
        return dict(result, state='unconfirmed', recovery_error='Offline recovery transaction requires inspection')
    if (run / 'offline-recovered.json').exists():
        try:
            receipt = verify_offline_recovery(run, meta)
        except (OSError, ValueError, RuntimeError, KeyError, TypeError) as error:
            return dict(result, state='unconfirmed', recovery_error=str(error))
        return dict(result, state='offline-recovered', receipt=receipt)
    if (run / 'stopped.json').exists():
        return dict(result, state='stopped', receipt=read_json(run / 'stopped.json'))
    if (run / 'failed.json').exists():
        return dict(result, state='failed', receipt=read_json(run / 'failed.json'))
    if (run / 'start.lock').exists() or started:
        return dict(result, state='unconfirmed')
    return dict(result, state='prepared')


@contextmanager
def lifecycle_guard(run):
    """Hold an OS lock for the supervisor lifetime; its file is never unlinked."""
    with (run / 'lifecycle.lock').open('a+b') as guard:
        if os.name == 'nt':
            import msvcrt
            if guard.seek(0, os.SEEK_END) == 0:
                guard.write(b'\0')
                guard.flush()
            guard.seek(0)
            try:
                msvcrt.locking(guard.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                raise RuntimeError('Another supervisor owns this VM; inspect status') from error
            try:
                yield
            finally:
                guard.seek(0)
                msvcrt.locking(guard.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            try:
                fcntl.flock(guard, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise RuntimeError('Another supervisor owns this VM; inspect status') from error
            try:
                yield
            finally:
                fcntl.flock(guard, fcntl.LOCK_UN)


def descendants(run, meta):
    disk = os.path.normcase(str((run / 'overlay.qcow2').resolve()))
    found = []
    for record in Path(meta['config']['runs_dir']).glob('*/lab.json'):
        if record.parent.resolve() == run:
            continue
        other = read_json(record)
        if any(os.path.normcase(str(Path(parent['path']).resolve())) == disk
               for key in ('parents', 'opencore_parents', 'recovery_parents')
               for parent in other.get(key, [])):
            found.append(str(record.parent))
    return found


def _recovery_check(condition, message):
    if not condition:
        raise RuntimeError('Offline recovery refused: ' + message)


def _recovery_plain(path):
    """Recovery never follows a reparse ancestor or a hard-linked input."""
    path = Path(path)
    for member in (path, *path.parents):
        stat = member.lstat()
        _recovery_check(not member.is_symlink() and not (getattr(stat, 'st_file_attributes', 0) & 0x400),
                        'reparse path: ' + str(member))
    stat = path.lstat()
    _recovery_check(path.is_file() and stat.st_nlink == 1, 'plain single-link file required: ' + str(path))
    return stat


def _recovery_pin(path, deadline):
    _recovery_check(time.monotonic() < deadline, 'inspection deadline elapsed')
    before = _recovery_plain(path)
    _recovery_check(before.st_size <= 256 * 1024 * 1024, 'session file exceeds inspection limit')
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while True:
            _recovery_check(time.monotonic() < deadline, 'inspection deadline elapsed')
            chunk = stream.read(128 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    after = _recovery_plain(path)
    key = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
    _recovery_check(key(before) == key(after), 'file changed during inspection: ' + str(path))
    return {'bytes': after.st_size, 'sha256': digest.hexdigest()}


def _recovery_create_json(path, value):
    raw = (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    with Path(path).open('xb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())


def _windows_recovery_processes():
    """CIM returns all processes; no process is selected for termination."""
    _recovery_check(os.name == 'nt', 'Windows host required')
    powershell = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    script = r'''$ErrorActionPreference='Stop';
[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false);
$rows=@(Get-CimInstance Win32_Process -ErrorAction Stop | ForEach-Object {
  [pscustomobject]@{pid=[int]$_.ProcessId;name=[string]$_.Name;
    executable=[string]$_.ExecutablePath;commandLine=[string]$_.CommandLine}
}); if($rows.Count -eq 0){throw 'Empty process enumeration'};
ConvertTo-Json -InputObject $rows -Compress -Depth 3'''
    answer = subprocess.run([str(powershell), '-NoLogo', '-NoProfile', '-NonInteractive', '-Command', script],
                            stdin=subprocess.DEVNULL, capture_output=True, timeout=15,
                            creationflags=WINDOWS_FLAGS)
    _recovery_check(answer.returncode == 0 and not answer.stderr, 'complete process enumeration failed')
    _recovery_check(0 < len(answer.stdout) <= 16 * 1024 * 1024, 'invalid process enumeration size')
    return json.loads(answer.stdout.decode('utf-8-sig'))


def _windows_command_line(command_line):
    """Use Windows' Unicode argv parser; never execute the observed command line."""
    import ctypes
    from ctypes import wintypes
    shell = ctypes.WinDLL('shell32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    shell.CommandLineToArgvW.argtypes = (wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int))
    shell.CommandLineToArgvW.restype = ctypes.POINTER(wintypes.LPWSTR)
    kernel.LocalFree.argtypes = (wintypes.HLOCAL,)
    kernel.LocalFree.restype = wintypes.HLOCAL
    count = ctypes.c_int()
    pointer = shell.CommandLineToArgvW(command_line, ctypes.byref(count))
    if not pointer:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        _recovery_check(0 < count.value <= 4096, 'ambiguous QEMU argv size')
        return [pointer[index] for index in range(count.value)]
    finally:
        _recovery_check(not kernel.LocalFree(pointer), 'QEMU argv buffer release failed')


def _foreign_qemu_bindings(row, forbidden_paths):
    """Conservative CLI classification, supplementary to the actual disk fence."""
    image, line = row['executable'], row['commandLine']
    _recovery_check(image and line and len(line) <= 32768 and Path(image).is_absolute(),
                    'QEMU image/full command line unavailable')
    argv = _windows_command_line(line)
    _recovery_check(os.path.normcase(argv[0]) == os.path.normcase(image), 'QEMU argv/image mismatch')
    _recovery_check(not any(token in argv for token in ('-readconfig', '-set')), 'opaque QEMU disk configuration')
    disk_paths = []
    def add(value):
        _recovery_check(type(value) is str and Path(value).is_absolute() and not value.startswith('\\\\'),
                        'QEMU disk binding is not an absolute local path')
        normalized = os.path.normcase(os.path.abspath(value))
        forbidden = [os.path.normcase(os.path.abspath(path)) for path in forbidden_paths]
        _recovery_check(not any(normalized == target or normalized.startswith(target + os.sep) for target in forbidden),
                        'QEMU disk argument references this VM')
        disk_paths.append(value)
    def options(raw):
        # QEMU doubles literal commas in key/value option strings.
        _recovery_check('\0' not in raw, 'invalid QEMU options')
        parts = raw.replace(',,', '\0').split(',')
        answer = {}
        for part in parts:
            key, separator, value = part.partition('=')
            _recovery_check(separator and key and key not in answer, 'ambiguous QEMU options')
            answer[key] = value.replace('\0', ',')
        return answer
    def block(value):
        _recovery_check(type(value) is dict and value.get('driver') in ('file', 'qcow2', 'raw'), 'opaque QEMU block backend')
        if value['driver'] == 'file':
            add(value.get('filename'))
        for key in ('file', 'backing'):
            if type(value.get(key)) is dict:
                block(value[key])
            elif value.get(key) is not None:
                _recovery_check(type(value[key]) is str and re.fullmatch('[A-Za-z0-9_.-]{1,64}', value[key]),
                                'opaque QEMU block reference')
    index = 1
    while index < len(argv):
        token = argv[index]
        if token in ('-blockdev', '-drive', '-hda', '-hdb', '-hdc', '-hdd', '-fda', '-fdb', '-cdrom'):
            _recovery_check(index + 1 < len(argv), 'missing QEMU disk argument')
            argument = argv[index + 1]
            if token == '-blockdev':
                if argument.startswith('{'):
                    block(json.loads(argument))
                else:
                    block(options(argument))
            elif token == '-drive':
                declared = options(argument)
                _recovery_check('file' in declared, 'opaque QEMU drive file')
                add(declared['file'])
            else:
                add(argument)
            index += 1
        index += 1
    _recovery_check(0 < len(disk_paths) <= 64, 'no explicit bounded QEMU disk binding')
    return {'pid': row['pid'], 'image': image, 'commandLineSha256': hashlib.sha256(line.encode('utf-8')).hexdigest(),
            'explicitDiskArgumentCount': len(disk_paths), 'classification': 'disjoint-visible-cli-only'}


def _recovery_process_snapshot(run, disks, old_pids, *, disk_fence_held=False):
    rows = _windows_recovery_processes()
    _recovery_check(type(rows) is list and 0 < len(rows) <= 100000, 'invalid process snapshot')
    seen = set()
    uninspectable = []
    foreign_qemu = []
    needles = [str(path).replace('/', '\\').casefold() for path in (run, *disks)] + [run.name.casefold()]
    for row in rows:
        _recovery_check(type(row) is dict and set(row) == {'pid', 'name', 'executable', 'commandLine'},
                        'incomplete process record')
        pid = row['pid']
        _recovery_check(type(pid) is int and pid >= 0 and pid not in seen, 'invalid process identity')
        seen.add(pid)
        _recovery_check(pid not in old_pids, 'recorded PID still exists (including PID reuse)')
        _recovery_check(all(type(row[k]) is str for k in ('name', 'executable', 'commandLine')) and row['name'],
                        'missing process identity')
        name, image, command_line = (row[k].casefold() for k in ('name', 'executable', 'commandLine'))
        potential = any(tag in name or tag in Path(image).name for tag in ('python', 'qemu'))
        _recovery_check(command_line or not potential, 'missing command line for a known potential VM controller')
        if not image or not command_line:
            uninspectable.append(pid)
        normalized = command_line.replace('/', '\\')
        _recovery_check(not any(needle in normalized or needle.replace('\\', '\\\\') in normalized for needle in needles),
                        'a process references the VM run/disk; inspect it before recovery')
        if 'qemu' in name or 'qemu' in Path(image).name:
            foreign_qemu.append(_foreign_qemu_bindings(row, (run, *disks)))
    _recovery_check(not any(process_exists(pid) for pid in old_pids), 'recorded PID exists after enumeration')
    # Keep command lines out of the receipt; preserve only a digest and bounded counts.
    raw = json.dumps(rows, sort_keys=True, ensure_ascii=False).encode('utf-8')
    unknown_raw = json.dumps(sorted(uninspectable), separators=(',', ':')).encode('utf-8')
    return {'processCount': len(rows), 'snapshotSha256': hashlib.sha256(raw).hexdigest(),
            'recordedPidsAbsent': old_pids, 'visibleVmReferences': [], 'observedAtUnix': time.time(),
            'uninspectableProcessCount': len(uninspectable),
            'uninspectablePidsSha256': hashlib.sha256(unknown_raw).hexdigest(),
            'diskFenceHeld': disk_fence_held, 'cimProvesNoWriters': False,
            'competingQemu': foreign_qemu, 'performanceIsolationProved': False}


def _windows_tcp_listener_table(family, port, deadline):
    """IP Helper OWNER_PID_LISTENER; expose only the target result and table digest."""
    import ctypes
    from ctypes import wintypes
    _recovery_check(os.name == 'nt' and family in (2, 23), 'Windows IPv4/IPv6 table required')
    class Row4(ctypes.Structure):
        _fields_ = [('state', wintypes.DWORD), ('local', wintypes.DWORD), ('port', wintypes.DWORD),
                    ('remote', wintypes.DWORD), ('remotePort', wintypes.DWORD), ('pid', wintypes.DWORD)]
    class Row6(ctypes.Structure):
        _fields_ = [('local', ctypes.c_ubyte * 16), ('scope', wintypes.DWORD), ('port', wintypes.DWORD),
                    ('remote', ctypes.c_ubyte * 16), ('remoteScope', wintypes.DWORD),
                    ('remotePort', wintypes.DWORD), ('state', wintypes.DWORD), ('pid', wintypes.DWORD)]
    row_type = Row4 if family == 2 else Row6
    class Table(ctypes.Structure):
        _fields_ = [('count', wintypes.DWORD), ('rows', row_type * 1)]
    library = ctypes.WinDLL('iphlpapi', use_last_error=True)
    query = library.GetExtendedTcpTable
    query.argtypes = (wintypes.LPVOID, ctypes.POINTER(wintypes.DWORD), wintypes.BOOL,
                      wintypes.ULONG, ctypes.c_int, wintypes.ULONG)
    query.restype = wintypes.DWORD
    size = wintypes.DWORD()
    _recovery_check(time.monotonic() < deadline, 'TCP table deadline elapsed')
    code = query(None, ctypes.byref(size), False, family, 3, 0)  # TCP_TABLE_OWNER_PID_LISTENER
    _recovery_check(code == 122, 'TCP table size query failed: ' + str(code))
    for _ in range(3):
        _recovery_check(time.monotonic() < deadline and 4 <= size.value <= 16 * 1024 * 1024,
                        'TCP table size/deadline invalid')
        allocated = size.value
        buffer = ctypes.create_string_buffer(allocated)
        code = query(buffer, ctypes.byref(size), False, family, 3, 0)
        _recovery_check(time.monotonic() < deadline, 'TCP table query returned after deadline')
        if code == 122:
            continue
        _recovery_check(code == 0 and 4 <= size.value <= allocated, 'TCP table query failed: ' + str(code))
        raw = buffer.raw
        count = wintypes.DWORD.from_buffer_copy(raw[:4]).value
        extent = Table.rows.offset + count * ctypes.sizeof(row_type)
        _recovery_check(count <= 100000 and extent <= size.value, 'truncated TCP listener table')
        matched = False
        for index in range(count):
            if index % 128 == 0:
                _recovery_check(time.monotonic() < deadline, 'TCP table parsing deadline elapsed')
            offset = Table.rows.offset + index * ctypes.sizeof(row_type)
            row = row_type.from_buffer_copy(raw[offset:offset + ctypes.sizeof(row_type)])
            _recovery_check(row.state == 2, 'non-listener in filtered TCP table')
            matched |= socket.ntohs(row.port & 0xffff) == port
        return {'family': family, 'entryCount': count, 'tableSha256': hashlib.sha256(raw[:extent]).hexdigest(),
                'targetPortListening': bool(matched)}
    raise RuntimeError('Offline recovery refused: TCP table grew beyond bounded retries')


def _recovery_qmp_absent(meta, *, deadline=None, disk_fence_held=False):
    # A timeout is never absence by itself. A successful kernel table query is required.
    deadline = deadline if deadline is not None else time.monotonic() + 5
    _recovery_check(time.monotonic() < deadline, 'QMP inspection deadline elapsed')
    port = meta['ports']['qmp']
    proof = {'port': port, 'observedAtUnix': time.time(), 'diskFenceHeld': disk_fence_held, 'tables': []}
    try:
        connection = socket.create_connection(('127.0.0.1', port), timeout=min(2, deadline - time.monotonic()))
    except ConnectionRefusedError:
        _recovery_check(time.monotonic() < deadline, 'QMP refusal returned after deadline')
        return dict(proof, method='tcp-refused')
    except TimeoutError:
        tables = [_windows_tcp_listener_table(family, port, deadline) for family in (2, 23)]
        _recovery_check(not any(table['targetPortListening'] for table in tables), 'kernel TCP table has a listener on QMP port')
        return dict(proof, method='windows-tcp-tables-no-listener', tables=tables)
    else:
        connection.close()
    raise RuntimeError('Offline recovery refused: QMP endpoint accepts connections')


@contextmanager
def _windows_recovery_disk_handle(path):
    """A held READ handle permits only other readers, denying disk writes/deletion."""
    _recovery_check(os.name == 'nt', 'Windows host required')
    import ctypes
    from ctypes import wintypes
    class Info(ctypes.Structure):
        _fields_ = [('attributes', wintypes.DWORD), ('creation', wintypes.FILETIME),
                    ('access', wintypes.FILETIME), ('write', wintypes.FILETIME),
                    ('volume', wintypes.DWORD), ('sizeHigh', wintypes.DWORD), ('sizeLow', wintypes.DWORD),
                    ('links', wintypes.DWORD), ('indexHigh', wintypes.DWORD), ('indexLow', wintypes.DWORD)]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                  wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.GetFileInformationByHandle.argtypes = (wintypes.HANDLE, ctypes.POINTER(Info))
    kernel.GetFileInformationByHandle.restype = wintypes.BOOL
    kernel.GetFinalPathNameByHandleW.argtypes = (wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, wintypes.DWORD)
    kernel.GetFinalPathNameByHandleW.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.CloseHandle.restype = wintypes.BOOL
    _recovery_plain(path)
    handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x00200000, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    def identity():
        info = Info()
        if not kernel.GetFileInformationByHandle(handle, ctypes.byref(info)):
            raise ctypes.WinError(ctypes.get_last_error())
        _recovery_check(not (info.attributes & (0x400 | 0x10)) and info.links == 1, 'non-plain disk handle')
        final = ctypes.create_unicode_buffer(32768)
        count = kernel.GetFinalPathNameByHandleW(handle, final, len(final), 0)
        _recovery_check(0 < count < len(final), 'cannot verify final disk path')
        actual = final.value
        if actual.startswith('\\\\?\\'):
            actual = actual[4:]
        _recovery_check(os.path.normcase(actual) == os.path.normcase(str(Path(path).absolute())), 'disk path alias')
        stamp = lambda value: (value.dwHighDateTime << 32) | value.dwLowDateTime
        return {'path': str(path), 'volume': info.volume, 'fileIndex': (info.indexHigh << 32) | info.indexLow,
                'bytes': (info.sizeHigh << 32) | info.sizeLow, 'creationFileTime': stamp(info.creation),
                'writeFileTime': stamp(info.write), 'shareMode': 'READ_ONLY_DENY_WRITE_DELETE'}
    try:
        original = identity()
        yield original
        _recovery_plain(path)
        _recovery_check(identity() == original, 'held disk identity changed')
    finally:
        if not kernel.CloseHandle(handle):
            raise ctypes.WinError(ctypes.get_last_error())


def verify_offline_recovery(run, meta):
    """Only the dedicated receipt plus its preserved archive opens the new state."""
    deadline = time.monotonic() + 30
    _recovery_check(not (run / 'offline-recovery.pending.json').exists(), 'pending recovery transaction')
    _recovery_check(not any((run / name).exists() for name in SESSION_FILES if name != 'offline-recovered.json'),
                    'new/current session markers coexist with recovery')
    _recovery_plain(run / 'offline-recovered.json')
    receipt = read_json(run / 'offline-recovered.json')
    _recovery_check(type(receipt) is dict and set(receipt) == {'schema', 'run', 'uuid', 'session', 'archive', 'reason',
                    'recoveredAtUnix', 'priorOutcome', 'originalExitCode', 'cleanShutdownVerified', 'files', 'intent', 'proof'},
                    'invalid recovery receipt shape')
    _recovery_check(receipt.get('schema') == 'qemu-novnc.offline-recovery/v1' and receipt.get('run') == str(run)
                    and receipt.get('uuid') == meta['uuid'] and receipt.get('priorOutcome') == 'UNKNOWN'
                    and receipt.get('originalExitCode') is None and receipt.get('cleanShutdownVerified') is False,
                    'invalid recovery receipt')
    archive_name = receipt.get('archive', '')
    _recovery_check(type(archive_name) is str and re.fullmatch(r'sessions/offline-recovery-[0-9a-f]{32}', archive_name),
                    'invalid recovery archive path')
    archive = run / archive_name
    files = receipt.get('files')
    _recovery_check(type(files) is dict and set(OFFLINE_METADATA) <= set(files)
                    and set(files) <= (set(SESSION_FILES) - {'offline-recovered.json'}) | {'lab.json'},
                    'invalid archived file membership')
    _recovery_check(set(p.name for p in archive.iterdir()) == set(files) | {'intent.json'},
                    'recovery archive membership changed')
    for name, pin in files.items():
        _recovery_check(type(pin) is dict and set(pin) == {'bytes', 'sha256'} and type(pin['bytes']) is int
                        and pin['bytes'] >= 0 and type(pin['sha256']) is str
                        and re.fullmatch('[0-9a-f]{64}', pin['sha256']), 'invalid archived file pin')
        _recovery_check(_recovery_pin(archive / name, deadline) == pin, 'recovery archive changed: ' + name)
    _recovery_check(_recovery_pin(run / 'lab.json', deadline) == files['lab.json'], 'lab metadata changed after recovery')
    _recovery_check(_recovery_pin(archive / 'intent.json', deadline) == receipt.get('intent'), 'recovery intent changed')
    intent = read_json(archive / 'intent.json')
    _recovery_check(type(intent) is dict and intent.get('schema') == 'qemu-novnc.offline-recovery-intent/v1'
                    and intent.get('session') == receipt.get('session') and intent.get('files') == files
                    and intent.get('run') == str(run) and intent.get('uuid') == meta['uuid'], 'recovery intent mismatch')
    started = read_json(archive / 'started.json')
    active = read_json(archive / 'active-session.json')
    _recovery_check(type(started) is dict and type(active) is dict, 'invalid archived metadata')
    old_pids = [started.get('qemu_pid'), started.get('supervisor_pid')]
    _recovery_check(all(type(pid) is int and pid > 0 for pid in old_pids) and len(set(old_pids)) == 2
                    and started.get('session') == receipt.get('session') == active.get('id')
                    and active.get('pid') == old_pids[1]
                    and (archive / 'start.lock').read_text(encoding='utf-8').strip() == str(old_pids[1]),
                    'archived session mismatch')
    proof = receipt.get('proof', {})
    _recovery_check(type(proof) is dict and set(proof) == {'catalogLockHeld', 'lifecycleLockHeld', 'qmpAbsenceChecks',
                    'diskHandlesClosed', 'processSnapshots', 'disks'}, 'invalid offline proof shape')
    _recovery_check(proof.get('catalogLockHeld') is True and proof.get('lifecycleLockHeld') is True
                    and proof.get('diskHandlesClosed') is True and type(proof.get('qmpAbsenceChecks')) is list
                    and len(proof['qmpAbsenceChecks']) == 3
                    and type(proof.get('processSnapshots')) is list and len(proof['processSnapshots']) == 3
                    and type(proof.get('disks')) is list and proof['disks'], 'missing offline proof')
    for position, snapshot in enumerate(proof['processSnapshots']):
        _recovery_check(type(snapshot) is dict and snapshot.get('visibleVmReferences') == []
                        and snapshot.get('recordedPidsAbsent') == [started.get('qemu_pid'), started.get('supervisor_pid')]
                        and type(snapshot.get('processCount')) is int and snapshot['processCount'] > 0
                        and re.fullmatch('[0-9a-f]{64}', snapshot.get('snapshotSha256', ''))
                        and type(snapshot.get('uninspectableProcessCount')) is int
                        and 0 <= snapshot['uninspectableProcessCount'] <= snapshot['processCount']
                        and re.fullmatch('[0-9a-f]{64}', snapshot.get('uninspectablePidsSha256', ''))
                        and snapshot.get('diskFenceHeld') is (position > 0)
                        and snapshot.get('cimProvesNoWriters') is False
                        and snapshot.get('performanceIsolationProved') is False
                        and type(snapshot.get('competingQemu')) is list, 'invalid process proof')
        for foreign in snapshot['competingQemu']:
            _recovery_check(type(foreign) is dict and type(foreign.get('pid')) is int and foreign['pid'] > 0
                            and type(foreign.get('image')) is str and Path(foreign['image']).is_absolute()
                            and re.fullmatch('[0-9a-f]{64}', foreign.get('commandLineSha256', ''))
                            and type(foreign.get('explicitDiskArgumentCount')) is int
                            and 0 < foreign['explicitDiskArgumentCount'] <= 64
                            and foreign.get('classification') == 'disjoint-visible-cli-only', 'invalid competing QEMU observation')
    for position, check in enumerate(proof['qmpAbsenceChecks']):
        _recovery_check(type(check) is dict and check.get('port') == meta['ports']['qmp']
                        and check.get('diskFenceHeld') is (position > 0), 'invalid QMP absence proof')
        if check.get('method') == 'tcp-refused':
            _recovery_check(check.get('tables') == [], 'invalid refused-connection proof')
        else:
            tables = check.get('tables')
            _recovery_check(check.get('method') == 'windows-tcp-tables-no-listener'
                            and type(tables) is list and len(tables) == 2, 'missing kernel TCP table proof')
            for table, family in zip(tables, (2, 23)):
                _recovery_check(type(table) is dict and table.get('family') == family
                                and type(table.get('entryCount')) is int and 0 <= table['entryCount'] <= 100000
                                and table.get('targetPortListening') is False
                                and re.fullmatch('[0-9a-f]{64}', table.get('tableSha256', '')), 'invalid kernel TCP table proof')
    expected_disks = [str(run / 'overlay.qcow2')]
    if meta['config'].get('firmware_vars'):
        expected_disks.append(str(run / 'firmware-vars.fd'))
    expected_disks += [str(Path(parent['path'])) for parent in meta['parents']]
    _recovery_check(len(proof['disks']) == len(expected_disks), 'offline disk proof count differs')
    for disk, expected in zip(proof['disks'], expected_disks):
        _recovery_check(type(disk) is dict and set(disk) == {'path', 'volume', 'fileIndex', 'bytes', 'creationFileTime',
                        'writeFileTime', 'shareMode'} and disk['path'] == expected
                        and disk['shareMode'] == 'READ_ONLY_DENY_WRITE_DELETE'
                        and all(type(disk[key]) is int and disk[key] >= 0
                                for key in ('volume', 'fileIndex', 'bytes', 'creationFileTime', 'writeFileTime')),
                        'invalid held disk proof')
    return receipt


def offline_recover(path, expected_session, expected_hashes, reason, *, catalog_lock_held=False):
    """Explicit Windows maintenance. Never infer or manufacture an old exit result."""
    _recovery_check(os.name == 'nt', 'Windows host required')
    _recovery_check(catalog_lock_held is True, 'invoke through the authorized shared pool')
    _recovery_check(type(expected_session) is str and str(uuid.UUID(expected_session)) == expected_session,
                    'canonical expected session UUID required')
    _recovery_check(type(expected_hashes) is dict and set(expected_hashes) == set(OFFLINE_METADATA)
                    and all(type(value) is str and re.fullmatch('[0-9a-f]{64}', value) for value in expected_hashes.values()),
                    'all four caller metadata digests required')
    _recovery_check(type(reason) is str and 0 < len(reason.strip()) <= 2000, 'explicit bounded reason required')
    run, meta = load_run(path)
    deadline = time.monotonic() + 60
    with lifecycle_guard(run):
        _recovery_check(vm_state(run, meta)['state'] == 'unconfirmed', 'only unconfirmed sessions may be recovered')
        _recovery_check(not (run / 'offline-recovery.pending.json').exists()
                        and not (run / 'offline-recovered.json').exists(), 'existing recovery requires inspection')
        _recovery_check(not descendants(run, meta), 'VM is a retained parent')
        for parent in meta['parents']:
            stat = _recovery_plain(Path(parent['path']))
            _recovery_check((stat.st_size, stat.st_mtime_ns) == (parent['bytes'], parent['mtime_ns']), 'parent changed')
        files = {}
        for name in ('lab.json', *SESSION_FILES):
            file = run / name
            if file.exists():
                files[name] = _recovery_pin(file, deadline)
        _recovery_check(set(OFFLINE_METADATA) <= set(files), 'missing current session metadata')
        _recovery_check(all(files[name]['bytes'] <= 1024 * 1024 for name in OFFLINE_METADATA), 'oversized session metadata')
        _recovery_check(not ({'stopped.json', 'failed.json', 'offline-recovered.json'} & set(files)), 'existing outcome receipt')
        _recovery_check(all(files[name]['sha256'] == value for name, value in expected_hashes.items()), 'caller metadata digest drift')
        started, active = read_json(run / 'started.json'), read_json(run / 'active-session.json')
        old_pids = [started.get('qemu_pid'), started.get('supervisor_pid')]
        _recovery_check(all(type(pid) is int and pid > 0 for pid in old_pids) and len(set(old_pids)) == 2,
                        'invalid old process identities')
        _recovery_check(started.get('session') == active.get('id') == expected_session
                        and active.get('pid') == old_pids[1]
                        and (run / 'start.lock').read_text(encoding='utf-8').strip() == str(old_pids[1]), 'session metadata mismatch')
        disks = [run / 'overlay.qcow2']
        if meta['config'].get('firmware_vars'):
            disks.append(run / 'firmware-vars.fd')
        # Parent handles additionally prevent alteration of the retained readonly chain.
        held_paths = disks + [Path(parent['path']) for parent in meta['parents']]
        qmp_first = _recovery_qmp_absent(meta, deadline=deadline)
        first = _recovery_process_snapshot(run, held_paths, old_pids)
        moved = []
        archive = None
        with ExitStack() as stack:
            disk_proofs = [stack.enter_context(_windows_recovery_disk_handle(disk)) for disk in held_paths]
            qmp_second = _recovery_qmp_absent(meta, deadline=deadline, disk_fence_held=True)
            second = _recovery_process_snapshot(run, held_paths, old_pids, disk_fence_held=True)
            _recovery_check(all(_recovery_pin(run / name, deadline) == pin for name, pin in files.items()),
                            'metadata/log drift before archive')
            archive_name = 'sessions/offline-recovery-' + uuid.uuid4().hex
            archive = run / archive_name
            archive.parent.mkdir(exist_ok=True)
            archive.mkdir(exist_ok=False)
            intent = {'schema': 'qemu-novnc.offline-recovery-intent/v1', 'run': str(run), 'uuid': meta['uuid'],
                      'session': expected_session, 'files': files, 'reason': reason.strip(),
                      'createdAtUnix': time.time(), 'priorOutcome': 'UNKNOWN', 'originalExitCode': None}
            pending = run / 'offline-recovery.pending.json'
            _recovery_create_json(pending, intent)
            try:
                # lab.json remains in place; exact original bytes also join the archive.
                with (archive / 'lab.json').open('xb') as output:
                    output.write((run / 'lab.json').read_bytes())
                    output.flush()
                    os.fsync(output.fileno())
                for name, pin in files.items():
                    if name != 'lab.json':
                        _recovery_check(_recovery_pin(run / name, deadline) == pin, 'session changed before move')
                        (run / name).rename(archive / name)
                        moved.append(name)
                _recovery_check(all(_recovery_pin(archive / name, deadline) == pin for name, pin in files.items()),
                                'archived session bytes differ')
                _recovery_check(_recovery_pin(run / 'lab.json', deadline) == files['lab.json'], 'lab metadata drift')
                qmp_third = _recovery_qmp_absent(meta, deadline=deadline, disk_fence_held=True)
                third = _recovery_process_snapshot(run, held_paths, old_pids, disk_fence_held=True)
                receipt = {'schema': 'qemu-novnc.offline-recovery/v1', 'run': str(run), 'uuid': meta['uuid'],
                           'session': expected_session, 'archive': archive_name, 'reason': reason.strip(),
                           'recoveredAtUnix': time.time(), 'priorOutcome': 'UNKNOWN', 'originalExitCode': None,
                           'cleanShutdownVerified': False, 'files': files, 'intent': _recovery_pin(pending, deadline),
                           'proof': {'catalogLockHeld': True, 'lifecycleLockHeld': True,
                                     'qmpAbsenceChecks': [qmp_first, qmp_second, qmp_third],
                                     'processSnapshots': [first, second, third], 'disks': disk_proofs, 'diskHandlesClosed': False}}
            except BaseException:
                # Preserve the pending intent even after successful rollback: no automatic retry/start.
                for name in reversed(moved):
                    if not (run / name).exists() and (archive / name).exists():
                        (archive / name).rename(run / name)
                raise
        # No offline-recovered state is published until every held handle closes successfully.
        receipt['proof']['diskHandlesClosed'] = True
        _recovery_check(time.monotonic() < deadline, 'recovery deadline elapsed before publication')
        _recovery_create_json(run / 'offline-recovered.json', receipt)
        (run / 'offline-recovery.pending.json').rename(archive / 'intent.json')
        verified = verify_offline_recovery(run, meta)
        return {'run': str(run), 'state': 'offline-recovered', 'receipt': verified, 'autoStarted': False,
                'guest_ui_verified': False}


def archive_session(run):
    names = SESSION_FILES
    if not any((run / name).exists() for name in names):
        return
    archive = run / 'sessions' / str(uuid.uuid4())
    archive.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(run / 'lab.json', archive / 'lab.json')
    for name in names:
        source = run / name
        if source.exists():
            source.rename(archive / name)


def serve(path):
    run, meta = load_run(path)
    with lifecycle_guard(run):
        state = vm_state(run, meta)['state']
        if state not in ('prepared', 'stopped', 'failed', 'offline-recovered'):
            raise RuntimeError('VM is ' + state + '; refusing a second writer')
        if descendants(run, meta):
            raise RuntimeError('VM is a retained parent; select its leaf VM')
        for parent in meta['parents']:
            stat = Path(parent['path']).stat()
            if (stat.st_size, stat.st_mtime_ns) != (parent['bytes'], parent['mtime_ns']):
                raise RuntimeError('Parent changed since prepare; inspect the existing VM')
        file_path(run / 'overlay.qcow2')
        if meta['config'].get('firmware_vars'):
            file_path(run / 'firmware-vars.fd')
        archive_session(run)
        meta['ports'] = dict(zip(('http', 'websocket', 'qmp', 'vnc'), free_ports()))
        if meta['ports']['vnc'] < 5900:
            raise RuntimeError('Ephemeral VNC port below 5900; retry start')
        meta['url'] = 'http://127.0.0.1:%s/' % meta['ports']['http']
        meta['argv'] = qemu_argv(meta, run)
        write_json(run / 'lab.json', meta)
        session = str(uuid.uuid4())
        write_json(run / 'active-session.json', {'id': session, 'started_at': time.time(), 'pid': os.getpid()})
        (run / 'start.lock').write_text(str(os.getpid()), encoding='utf-8')
        _serve_session(run, meta, session)


def _serve_session(run, meta, session):
    process, server, http_thread = None, None, None
    try:
        for port in meta['ports'].values():
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', port))
        server = http.server.ThreadingHTTPServer(('127.0.0.1', meta['ports']['http']), LabHTTP)
        server.lab_meta = meta
        with (run / 'qemu.stdout.log').open('wb') as stdout, (run / 'qemu.stderr.log').open('wb') as stderr:
            process = subprocess.Popen(meta['argv'], stdout=stdout, stderr=stderr, creationflags=WINDOWS_FLAGS)
        write_json(run / 'started.json', {'qemu_pid': process.pid, 'supervisor_pid': os.getpid(), 'session': session})
        http_thread = threading.Thread(target=server.serve_forever, daemon=True)
        http_thread.start()
        code = process.wait()
        write_json(run / 'stopped.json', {'exit_code': code, 'observed_at_unix': time.time(), 'session': session})
    except Exception as error:
        write_json(run / 'failed.json', {'error': str(error), 'session': session})
        if process is not None and process.poll() is None:
            process.wait()
        raise
    finally:
        if server is not None:
            if http_thread is not None and http_thread.is_alive():
                server.shutdown()
            server.server_close()


def start(path):
    run, meta = load_run(path)
    state = vm_state(run, meta)
    if state['state'] == 'running':
        return dict(state, reused=True)
    if state['state'] not in ('prepared', 'stopped', 'failed', 'offline-recovered'):
        raise RuntimeError('VM is ' + state['state'] + '; inspect the existing VM instead of restarting')
    if descendants(run, meta):
        raise RuntimeError('VM is a retained parent; select its leaf VM')
    before = read_json(run / 'active-session.json') if (run / 'active-session.json').exists() else None
    kwargs = {'creationflags': WINDOWS_FLAGS} if os.name == 'nt' else {'start_new_session': True}
    with (run / 'supervisor.log').open('ab') as log:
        child = subprocess.Popen([sys.executable, '-B', str(Path(__file__).resolve()), '_serve', '--run', str(run)],
                                 stdin=subprocess.DEVNULL, stdout=log, stderr=log, **kwargs)
    for _ in range(100):
        run, meta = load_run(run)
        current = read_json(run / 'active-session.json') if (run / 'active-session.json').exists() else None
        state = vm_state(run, meta)
        if state['state'] == 'running':
            with urllib.request.urlopen(meta['url'] + 'connection.json', timeout=1) as response:
                json.load(response)
            return dict(state, reused=True, session=current)
        if current != before:
            if (run / 'failed.json').exists():
                raise RuntimeError(read_json(run / 'failed.json')['error'])
            if (run / 'stopped.json').exists():
                raise RuntimeError('QEMU exited; inspect qemu.stderr.log')
        if child.poll() is not None:
            raise RuntimeError('Supervisor exited; inspect supervisor.log')
        time.sleep(0.2)
    raise RuntimeError('Startup unconfirmed; inspect this VM. No process was force-killed')


def operate(action, path, iso=None):
    run, meta = load_run(path)
    state = vm_state(run, meta)
    if action == 'status':
        return state
    if state['state'] != 'running':
        raise RuntimeError('VM is ' + state['state'] + '; inspect status before control')
    qmp = QMP(meta)
    try:
        if action == 'media':
            image = str(file_path(iso))
            qmp.call('blockdev-change-medium', {'id': 'installcd', 'filename': image, 'format': 'raw'})
            return {'state': 'media-changed', 'iso': image, 'guest_ui_verified': False}
        qmp.call('system_powerdown')
        return {'state': 'shutdown-requested', 'confirmed_stopped': False,
                'next': 'Check status until stopped; ACPI may require guest interaction'}
    finally:
        qmp.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    commands.add_parser('prepare').add_argument('--config', required=True)
    for name in ('start', 'status', 'shutdown', 'media', '_serve'):
        child = commands.add_parser(name)
        child.add_argument('--run', required=True)
        if name == 'media':
            child.add_argument('--iso', required=True)
    args = parser.parse_args()
    if args.action == 'prepare':
        result = prepare(args.config)
    elif args.action == 'start':
        result = start(args.run)
    elif args.action == '_serve':
        serve(args.run)
        return
    else:
        result = operate(args.action, args.run, getattr(args, 'iso', None))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps({'error': str(error)}, ensure_ascii=False), file=sys.stderr)
        sys.exit(1)
