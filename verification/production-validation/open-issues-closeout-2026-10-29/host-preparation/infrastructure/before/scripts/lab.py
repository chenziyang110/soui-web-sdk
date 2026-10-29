#!/usr/bin/env python3
"""Local QEMU/noVNC lifecycle. No guest input automation or run deletion."""
import argparse
from contextlib import contextmanager
import http.server
import json
import os
from pathlib import Path
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


def archive_session(run):
    names = ('started.json', 'stopped.json', 'failed.json', 'start.lock',
             'active-session.json', 'qemu.stdout.log', 'qemu.stderr.log', 'serial.log')
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
        if state not in ('prepared', 'stopped', 'failed'):
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
    if state['state'] not in ('prepared', 'stopped', 'failed'):
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
