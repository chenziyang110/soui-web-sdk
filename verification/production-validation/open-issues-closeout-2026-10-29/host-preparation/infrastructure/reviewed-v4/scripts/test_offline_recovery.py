"""Focused recovery regression; temporary files only, never a VM or guest start."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
from unittest.mock import Mock, patch
import uuid

import lab
import pool
ACTUAL_PROCESS_QUERY = lab._windows_recovery_processes


@unittest.skipUnless(os.name == 'nt', 'Windows-only offline recovery')
class OfflineRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        self.run = self.root / 'runs' / str(uuid.uuid4())
        self.run.mkdir(parents=True)
        self.base = self.root / 'base.qcow2'
        self.base.write_bytes(b'read-only-parent-fixture')
        self.session = str(uuid.uuid4())
        parent = self.base.stat()
        self.meta = {'schema': 1, 'profile': 'generic-x86', 'uuid': self.run.name,
                     'config': {'runs_dir': str(self.run.parent), 'network': 'none', 'firmware_vars': 'fixture-only'},
                     'parents': [{'path': str(self.base), 'format': 'qcow2',
                                  'bytes': parent.st_size, 'mtime_ns': parent.st_mtime_ns}],
                     'url': 'http://127.0.0.1:30001/',
                     'ports': {'http': 30001, 'qmp': 30002, 'websocket': 30003, 'vnc': 30004}}
        for name, value in [('lab.json', self.meta),
                            ('started.json', {'qemu_pid': 987654321, 'supervisor_pid': 987654322, 'session': self.session}),
                            ('active-session.json', {'id': self.session, 'pid': 987654322, 'started_at': 1})]:
            lab.write_json(self.run / name, value)
        (self.run / 'start.lock').write_text('987654322', encoding='utf-8')
        (self.run / 'qemu.stderr.log').write_bytes(b'original failure observations\n')
        (self.run / 'serial.log').write_bytes(b'original automation evidence\n')
        (self.run / 'overlay.qcow2').write_bytes(b'overlay-fixture-no-mutation')
        (self.run / 'firmware-vars.fd').write_bytes(b'nvram-fixture-no-mutation')
        self.originals = {p.name: p.read_bytes() for p in self.run.iterdir() if p.is_file()}
        self.pins = {name: hashlib.sha256(self.originals[name]).hexdigest() for name in lab.OFFLINE_METADATA}
        self.process_patch = patch.object(lab, 'process_exists', return_value=False)
        self.process_patch.start()
        self.snapshot_patch = patch.object(lab, '_windows_recovery_processes', return_value=[
            {'pid': 0, 'name': 'System Idle Process', 'executable': '', 'commandLine': ''},
            {'pid': 321, 'name': 'unrelated.exe', 'executable': 'C:/fixture/unrelated.exe', 'commandLine': 'unrelated.exe'}])
        self.snapshot = self.snapshot_patch.start()
        self.socket_patch = patch.object(lab.socket, 'create_connection', side_effect=ConnectionRefusedError)
        self.socket_patch.start()

    def tearDown(self):
        self.socket_patch.stop()
        self.snapshot_patch.stop()
        self.process_patch.stop()
        self.temp.cleanup()

    def recover(self, **values):
        args = dict(expected_session=self.session, expected_hashes=self.pins,
                    reason='Original controller lost; preserve unknown prior outcome', catalog_lock_held=True)
        args.update(values)
        return lab.offline_recover(self.run, **args)

    def assert_unchanged(self):
        for name, raw in self.originals.items():
            self.assertEqual((self.run / name).read_bytes(), raw, name)
        self.assertFalse((self.run / 'offline-recovered.json').exists())

    def test_success_preserves_exact_originals_disks_and_unknown_outcome(self):
        with patch.object(lab.subprocess, 'Popen') as spawn:
            result = self.recover()
        spawn.assert_not_called()
        self.assertFalse(result['autoStarted'])
        self.assertEqual(result['state'], 'offline-recovered')
        self.assertEqual(result['receipt']['priorOutcome'], 'UNKNOWN')
        self.assertIsNone(result['receipt']['originalExitCode'])
        self.assertIs(result['receipt']['cleanShutdownVerified'], False)
        archive = self.run / result['receipt']['archive']
        for name in result['receipt']['files']:
            self.assertEqual((archive / name).read_bytes(), self.originals[name], name)
        for name in ('lab.json', 'overlay.qcow2', 'firmware-vars.fd'):
            self.assertEqual((self.run / name).read_bytes(), self.originals[name])
        self.assertEqual(lab.vm_state(self.run, self.meta)['state'], 'offline-recovered')
        self.assertFalse((self.run / 'stopped.json').exists())
        self.assertFalse((self.run / 'failed.json').exists())
        self.assertFalse((self.run / 'offline-recovery.pending.json').exists())

    def test_live_qmp_refused_without_mutation(self):
        fake = Mock()
        fake.call.return_value = {'running': True}
        with patch.object(lab, 'QMP', return_value=fake), self.assertRaisesRegex(RuntimeError, 'only unconfirmed'):
            self.recover()
        self.assert_unchanged()

    def test_old_pid_or_pid_reuse_refused(self):
        with patch.object(lab, 'process_exists', return_value=True), self.assertRaisesRegex(RuntimeError, 'only unconfirmed'):
            self.recover()
        self.assert_unchanged()

    def test_qmp_listener_or_kernel_query_failure_is_not_absence(self):
        for failure in (None, TimeoutError('unreachable is not refusal')):
            with self.subTest(failure=failure):
                with patch.object(lab, 'vm_state', return_value={'state': 'unconfirmed'}):
                    if failure is None:
                        with patch.object(lab.socket, 'create_connection', return_value=Mock()), self.assertRaisesRegex(RuntimeError, 'QMP endpoint'):
                            self.recover()
                    else:
                        with patch.object(lab.socket, 'create_connection', side_effect=failure), \
                                patch.object(lab, '_windows_tcp_listener_table', side_effect=OSError('kernel query failed')), \
                                self.assertRaises(OSError):
                            self.recover()
        self.assert_unchanged()

    def test_share_lock_blocks_disk_writer_and_recovery_refuses_existing_writer(self):
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                      wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel.CloseHandle.restype = wintypes.BOOL
        overlay = self.run / 'overlay.qcow2'
        with lab._windows_recovery_disk_handle(overlay):
            handle = kernel.CreateFileW(str(overlay), 0x40000000, 7, None, 3, 0, None)
            self.assertEqual(handle, wintypes.HANDLE(-1).value)
            self.assertEqual(ctypes.get_last_error(), 32)
        writer = kernel.CreateFileW(str(overlay), 0x40000000, 7, None, 3, 0, None)
        self.assertNotEqual(writer, wintypes.HANDLE(-1).value)
        try:
            with self.assertRaises(OSError):
                self.recover()
        finally:
            self.assertTrue(kernel.CloseHandle(writer))
        self.assert_unchanged()

    def test_digest_drift_session_mismatch_missing_metadata_refused(self):
        for kind in ('digest', 'session', 'missing'):
            with self.subTest(kind=kind):
                if kind == 'digest':
                    pins = dict(self.pins, **{'lab.json': 'f' * 64})
                    with self.assertRaisesRegex(RuntimeError, 'digest drift'):
                        self.recover(expected_hashes=pins)
                elif kind == 'session':
                    with self.assertRaisesRegex(RuntimeError, 'session metadata mismatch'):
                        self.recover(expected_session=str(uuid.uuid4()))
                else:
                    raw = (self.run / 'active-session.json').read_bytes()
                    (self.run / 'active-session.json').unlink()
                    try:
                        with self.assertRaisesRegex(RuntimeError, 'missing current session metadata'):
                            self.recover()
                    finally:
                        (self.run / 'active-session.json').write_bytes(raw)
                self.assert_unchanged()

    def test_nonleaf_and_changed_parent_refused(self):
        with patch.object(lab, 'descendants', return_value=['child']), self.assertRaisesRegex(RuntimeError, 'retained parent'):
            self.recover()
        self.base.write_bytes(b'changed-parent')
        with self.assertRaisesRegex(RuntimeError, 'parent changed'):
            self.recover()
        self.assert_unchanged()

    def test_missing_controller_command_line_and_any_qemu_or_vm_reference_refused(self):
        for row in [
            {'pid': 99, 'name': 'python.exe', 'executable': 'C:/Python/python.exe', 'commandLine': ''},
            {'pid': 99, 'name': 'qemu-system-x86_64.exe', 'executable': 'C:/Qemu/qemu.exe', 'commandLine': 'qemu.exe'},
            {'pid': 99, 'name': 'qemu-system-x86_64.exe', 'executable': '', 'commandLine': ''},
            {'pid': 99, 'name': 'reader.exe', 'executable': 'C:/reader.exe', 'commandLine': 'reader.exe ' + str(self.run / 'overlay.qcow2')},
            {'pid': 987654321, 'name': 'reused.exe', 'executable': 'C:/reused.exe', 'commandLine': 'reused.exe'}]:
            with self.subTest(row=row), patch.object(lab, '_windows_recovery_processes', return_value=[row]):
                with self.assertRaises(RuntimeError):
                    self.recover()
                self.assert_unchanged()

    def test_uninspectable_processes_recorded_and_both_final_snapshots_hold_disk_fences(self):
        self.snapshot.return_value += [
            {'pid': 77, 'name': 'ProtectedProcess.exe', 'executable': '', 'commandLine': ''},
            {'pid': 88, 'name': 'UnrelatedService.exe', 'executable': 'C:/Windows/service.exe', 'commandLine': ''}]
        original = lab._recovery_process_snapshot
        observations = []
        def observe(run, disks, pids, *, disk_fence_held=False):
            observations.append((disk_fence_held, (run / 'offline-recovery.pending.json').exists()))
            if disk_fence_held:
                for disk in disks:
                    with self.assertRaises(OSError):
                        # Access-only probe on temporary fixtures, never a content write.
                        with disk.open('r+b'):
                            pass
            return original(run, disks, pids, disk_fence_held=disk_fence_held)
        with patch.object(lab, '_recovery_process_snapshot', side_effect=observe):
            result = self.recover()
        self.assertEqual(observations, [(False, False), (True, False), (True, True)])
        snapshots = result['receipt']['proof']['processSnapshots']
        self.assertEqual([row['diskFenceHeld'] for row in snapshots], [False, True, True])
        self.assertTrue(all(row['uninspectableProcessCount'] == 3 for row in snapshots))
        self.assertTrue(all(row['cimProvesNoWriters'] is False for row in snapshots))
        self.assertTrue(all(row['visibleVmReferences'] == [] for row in snapshots))
        self.assertEqual(lab.vm_state(self.run, self.meta)['state'], 'offline-recovered')

    def test_postarchive_snapshot_failure_rolls_back_and_keeps_pending(self):
        ordinary = self.snapshot.return_value
        self.snapshot.side_effect = [ordinary, ordinary, [
            {'pid': 123, 'name': 'qemu-system-x86_64.exe', 'executable': '', 'commandLine': ''}]]
        with self.assertRaisesRegex(RuntimeError, 'potential VM controller'):
            self.recover()
        for name, raw in self.originals.items():
            self.assertEqual((self.run / name).read_bytes(), raw)
        self.assertTrue((self.run / 'offline-recovery.pending.json').is_file())
        self.assertFalse((self.run / 'offline-recovered.json').exists())

    def foreign_row(self, disk=None, extra=None):
        image = 'C:/Program Files/qemu/qemu-system-x86_64.exe'
        args = [image, '-uuid', str(uuid.uuid4()), '-blockdev', json.dumps({
            'driver': 'file', 'filename': str(disk or (self.root / 'other-vm' / 'overlay.qcow2')),
            'node-name': 'otherfile'}), '-blockdev', json.dumps({'driver': 'qcow2', 'file': 'otherfile', 'node-name': 'other'})]
        args += extra or []
        return {'pid': 123456, 'name': 'qemu-system-x86_64.exe', 'executable': image,
                'commandLine': subprocess.list2cmdline(args)}

    def test_timeout_kernel_proof_and_foreign_disjoint_qemu_coexist_without_performance_claim(self):
        foreign = self.foreign_row()
        self.snapshot.return_value += [foreign]
        def table(family, port, deadline):
            return {'family': family, 'entryCount': 3, 'tableSha256': 'a' * 64, 'targetPortListening': False}
        with patch.object(lab.socket, 'create_connection', side_effect=TimeoutError('test timeout')), \
                patch.object(lab, '_windows_tcp_listener_table', side_effect=table) as native:
            result = self.recover()
            self.assertEqual(lab.vm_state(self.run, self.meta)['state'], 'offline-recovered')
        self.assertEqual(native.call_count, 6)
        proof = result['receipt']['proof']
        self.assertTrue(all(row['method'] == 'windows-tcp-tables-no-listener' for row in proof['qmpAbsenceChecks']))
        self.assertEqual([[table['family'] for table in row['tables']] for row in proof['qmpAbsenceChecks']], [[2, 23]] * 3)
        for snapshot in proof['processSnapshots']:
            self.assertIs(snapshot['performanceIsolationProved'], False)
            self.assertEqual(snapshot['competingQemu'], [{'pid': foreign['pid'], 'image': foreign['executable'],
                'commandLineSha256': hashlib.sha256(foreign['commandLine'].encode('utf-8')).hexdigest(),
                'explicitDiskArgumentCount': 1, 'classification': 'disjoint-visible-cli-only'}])
            self.assertNotIn('commandLine', snapshot['competingQemu'][0])

    def test_foreign_qemu_same_disk_uuid_and_ambiguous_binding_refused(self):
        rows = [self.foreign_row(self.run / 'overlay.qcow2'),
                self.foreign_row(extra=['-name', self.run.name]),
                self.foreign_row(extra=['-readconfig', 'opaque.ini'])]
        relative = self.foreign_row()
        relative['commandLine'] = subprocess.list2cmdline([relative['executable'], '-drive', 'file=relative.qcow2,format=qcow2'])
        rows.append(relative)
        missing = self.foreign_row()
        missing['commandLine'] = ''
        rows.append(missing)
        for row in rows:
            with self.subTest(row=row), patch.object(lab, '_windows_recovery_processes', return_value=[row]):
                with self.assertRaises(RuntimeError):
                    self.recover()
                self.assert_unchanged()

    def test_kernel_listener_on_either_family_or_query_failure_refused(self):
        for match in (2, 23):
            def table(family, port, deadline):
                return {'family': family, 'entryCount': 1, 'tableSha256': 'b' * 64,
                        'targetPortListening': family == match}
            with self.subTest(family=match), patch.object(lab.socket, 'create_connection', side_effect=TimeoutError), \
                    patch.object(lab, '_windows_tcp_listener_table', side_effect=table) as native:
                with self.assertRaisesRegex(RuntimeError, 'listener on QMP port'):
                    lab._recovery_qmp_absent(self.meta)
                self.assertEqual(native.call_count, 2)
        with patch.object(lab.socket, 'create_connection', side_effect=TimeoutError), \
                patch.object(lab, '_windows_tcp_listener_table', side_effect=OSError('query failure')):
            with self.assertRaisesRegex(OSError, 'query failure'):
                lab._recovery_qmp_absent(self.meta)

    def test_native_iphelper_detects_owned_ipv4_and_ipv6_loopback_listeners(self):
        # Own temporary listeners only. Never connect to or control a VM endpoint.
        for family, native_family, address in ((socket.AF_INET, 2, '127.0.0.1'), (socket.AF_INET6, 23, '::1')):
            with self.subTest(family=family), socket.socket(family) as listener:
                if family == socket.AF_INET6:
                    listener.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
                listener.bind((address, 0))
                listener.listen(1)
                port = listener.getsockname()[1]
                observations = [lab._windows_tcp_listener_table(item, port, time.monotonic() + 5) for item in (2, 23)]
                self.assertTrue(next(row for row in observations if row['family'] == native_family)['targetPortListening'])
                self.assertTrue(all(set(row) == {'family', 'entryCount', 'tableSha256', 'targetPortListening'} for row in observations))

    def test_lifecycle_owner_and_catalog_requirement_refused(self):
        with self.assertRaisesRegex(RuntimeError, 'authorized shared pool'):
            self.recover(catalog_lock_held=False)
        with lab.lifecycle_guard(self.run), self.assertRaisesRegex(RuntimeError, 'Another supervisor'):
            self.recover()
        self.assert_unchanged()

    def test_archive_failure_rolls_back_but_pending_guard_blocks_start(self):
        rename = Path.rename
        count = 0
        def fail_second(source, destination):
            nonlocal count
            count += 1
            if count == 2:
                raise OSError('injected archive failure')
            return rename(source, destination)
        with patch.object(Path, 'rename', fail_second), self.assertRaisesRegex(OSError, 'archive failure'):
            self.recover()
        for name, raw in self.originals.items():
            self.assertEqual((self.run / name).read_bytes(), raw)
        self.assertTrue((self.run / 'offline-recovery.pending.json').is_file())
        with patch.object(lab.subprocess, 'Popen') as spawn:
            with self.assertRaisesRegex(RuntimeError, 'unconfirmed'):
                lab.start(self.run)
        spawn.assert_not_called()

    def test_receipt_or_archive_tamper_blocks_start(self):
        result = self.recover()
        archive = self.run / result['receipt']['archive']
        (archive / 'started.json').write_text('{}', encoding='utf-8')
        with patch.object(lab.subprocess, 'Popen') as spawn:
            self.assertEqual(lab.vm_state(self.run, self.meta)['state'], 'unconfirmed')
            with self.assertRaisesRegex(RuntimeError, 'unconfirmed'):
                lab.start(self.run)
        spawn.assert_not_called()

    def test_handle_close_uncertainty_retains_pending_and_never_publishes(self):
        held = lab._windows_recovery_disk_handle
        @contextmanager
        def uncertain_close(path):
            with held(path) as proof:
                yield proof
            if path == self.run / 'overlay.qcow2':
                raise OSError('injected handle close uncertainty')
        with patch.object(lab, '_windows_recovery_disk_handle', uncertain_close):
            with self.assertRaisesRegex(OSError, 'handle close uncertainty'):
                self.recover()
        self.assertTrue((self.run / 'offline-recovery.pending.json').is_file())
        self.assertFalse((self.run / 'offline-recovered.json').exists())
        self.assertEqual(lab.vm_state(self.run, self.meta)['state'], 'unconfirmed')

    def test_publication_failure_with_partial_receipt_stays_pending(self):
        create = lab._recovery_create_json
        def fail_publication(path, value):
            create(path, value)
            if path.name == 'offline-recovered.json':
                raise OSError('injected publication acknowledgement failure')
        with patch.object(lab, '_recovery_create_json', fail_publication):
            with self.assertRaisesRegex(OSError, 'publication acknowledgement failure'):
                self.recover()
        self.assertTrue((self.run / 'offline-recovery.pending.json').is_file())
        self.assertEqual(lab.vm_state(self.run, self.meta)['state'], 'unconfirmed')

    def test_receipt_cannot_upgrade_old_exit_or_clean_shutdown(self):
        self.recover()
        file = self.run / 'offline-recovered.json'
        original = lab.read_json(file)
        for key, value in [('originalExitCode', 0), ('cleanShutdownVerified', True), ('priorOutcome', 'PASSED')]:
            changed = dict(original, **{key: value})
            lab.write_json(file, changed)
            self.assertEqual(lab.vm_state(self.run, self.meta)['state'], 'unconfirmed')

    def test_query_errors_and_non_utf8_output_are_not_absence(self):
        import subprocess
        for answer in [subprocess.CompletedProcess([], 1, b'[]', b'query failed'),
                       subprocess.CompletedProcess([], 0, b'[]', b'partial snapshot warning'),
                       subprocess.CompletedProcess([], 0, b'\xff\xff', b'')]:
            with self.subTest(answer=answer), patch.object(lab.subprocess, 'run', return_value=answer):
                with self.assertRaises((RuntimeError, UnicodeError)):
                    ACTUAL_PROCESS_QUERY()

    def test_ordinary_unconfirmed_start_still_refused(self):
        with patch.object(lab.subprocess, 'Popen') as spawn:
            self.assertEqual(lab.vm_state(self.run, self.meta)['state'], 'unconfirmed')
            with self.assertRaisesRegex(RuntimeError, 'unconfirmed'):
                lab.start(self.run)
        spawn.assert_not_called()

    def test_recovered_state_enters_existing_start_path_without_autostart(self):
        self.recover()
        with patch.object(lab.subprocess, 'Popen', side_effect=OSError('synthetic spawn boundary')) as spawn:
            with self.assertRaisesRegex(OSError, 'synthetic spawn boundary'):
                lab.start(self.run)
        self.assertEqual(spawn.call_count, 1)

    def test_pool_authorization_and_retained_child_guard_precede_recovery(self):
        home = self.root / 'catalog'
        home.mkdir()
        entry = {'run': str(self.run), 'profile': 'generic-x86', 'lease': {'token': 'fixture-token', 'owner': 'fixture'}}
        lab.write_json(home / 'catalog.json', {'schema': 1, 'vms': {'fixture-vm': entry}})
        values = dict(action='offline-recover', name='fixture-vm', token='wrong', reason='fixture',
                      expected_session=self.session, expected_lab_sha256=self.pins['lab.json'],
                      expected_started_sha256=self.pins['started.json'],
                      expected_active_session_sha256=self.pins['active-session.json'],
                      expected_start_lock_sha256=self.pins['start.lock'])
        module = Mock()
        with patch.object(pool, 'launcher', return_value=module), patch.object(pool, 'retained_by', return_value=[]):
            with self.assertRaisesRegex(RuntimeError, 'Valid lease'):
                pool.dispatch(home, argparse.Namespace(**values))
            module.offline_recover.assert_not_called()
            values['token'] = 'fixture-token'
            with patch.object(pool, 'retained_by', return_value=['retained-child']):
                with self.assertRaisesRegex(RuntimeError, 'retained parents'):
                    pool.dispatch(home, argparse.Namespace(**values))
            module.offline_recover.assert_not_called()
            module.offline_recover.return_value = {'state': 'offline-recovered'}
            self.assertEqual(pool.dispatch(home, argparse.Namespace(**values))['state'], 'offline-recovered')
            module.offline_recover.assert_called_once_with(str(self.run), self.session, self.pins, 'fixture', catalog_lock_held=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
