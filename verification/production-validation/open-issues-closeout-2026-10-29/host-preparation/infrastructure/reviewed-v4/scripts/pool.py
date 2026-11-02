"""Shared per-user VM catalog and cooperative Agent ownership (Python 3.10+)."""
import argparse
from contextlib import contextmanager
import importlib.util
import hashlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import sys
import tempfile
import time

sys.dont_write_bytecode = True
import lab_common as common
import pool_external_strict as external


def pool_home():
    # Keep this location independent of project, task, platform and Skill copy.
    path = Path(os.environ.get('QEMU_LAB_HOME', Path.home() / '.qemu-novnc-lab')).expanduser()
    if not path.is_absolute():
        raise ValueError('QEMU_LAB_HOME must be an absolute shared directory, never a task-relative path')
    return path.resolve()


@contextmanager
def locked(home):
    home.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (home / 'catalog.lock').open('a+b') as guard:
        guard.seek(0, 2)
        if not guard.tell():
            guard.write(b'0')
            guard.flush()
        guard.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(guard.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(guard, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise RuntimeError('Catalog busy; retry the same operation, never create another pool') from error
        try:
            yield
        finally:
            if os.name == 'nt':
                guard.seek(0)
                msvcrt.locking(guard.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(guard, fcntl.LOCK_UN)


def catalog(home):
    path = home / 'catalog.json'
    data = common.read_json(path) if path.exists() else {'schema': 1, 'vms': {}}
    if data.get('schema') != 1 or not isinstance(data.get('vms'), dict):
        raise ValueError('Invalid catalog; repair this catalog instead of creating another pool')
    return data


def launcher(profile):
    if profile not in (common.GENERIC_PROFILE, common.MAC_PROFILE):
        raise ValueError('Unsupported profile')
    script = Path(__file__).with_name('lab.py') if profile == common.GENERIC_PROFILE else (
        Path(__file__).resolve().parents[1] / 'platforms/macos-intel/scripts/lab.py')
    spec = importlib.util.spec_from_file_location('pool_' + profile.replace('-', '_'), script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def profile_of(config):
    profile = config.get('profile') or (common.MAC_PROFILE if 'opencore_image' in config else common.GENERIC_PROFILE)
    common.validate_profile(config, profile)
    return profile


def image_identity(config):
    return [profile_of(config)] + [os.path.normcase(str(Path(config[k]).expanduser().resolve())) if config.get(k) else None
                                   for k in ('base_image', 'opencore_image', 'recovery_image')]


def config_digest(config):
    return hashlib.sha256(json.dumps(config, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def entry_config(entry):
    if entry.get('run'):
        _, meta = common.load_run(entry['run'], entry['profile'])
        return meta['config']
    return common.read_json(entry['config'])


def required_text(value, label):
    if not isinstance(value, str) or not value.strip() or len(value) > 2000:
        raise ValueError(label + ' must contain 1..2000 characters')
    return value.strip()


def register(data, name, config_path=None, run_path=None, reason=None):
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,62}', name):
        raise ValueError('Use a stable lowercase VM name, not an Agent/task name')
    if name in data['vms']:
        raise ValueError('Name already registered; use the existing entry')
    entry = {'registered_at': time.time(), 'run': None}
    if run_path:
        run = Path(run_path).expanduser().resolve(strict=True)
        meta = common.read_json(run / 'lab.json')
        profile = profile_of(meta['config'])
        common.load_run(run, profile)
        config = meta['config']
        entry.update(run=str(run), profile=profile)
    else:
        path = common.file_path(config_path)
        config = common.read_json(path)
        profile = profile_of(config)
        config = launcher(profile).validate_config(config)
        entry.update(config=str(path), profile=profile, config_digest=config_digest(config))
    identity = image_identity(config)
    entry['image_set'] = identity
    for other_name, other in data['vms'].items():
        if external.is_entry(other):
            if entry.get('run') and external.contract.same_native_run_path(entry['run'], other['R']):
                raise ValueError('Path reserved by external strict case')
            continue
        if entry.get('run') and other.get('run') and Path(other['run']).resolve() == Path(entry['run']):
            raise ValueError('VM already registered as ' + other_name)
        # A bad registered entry must be inspected, not silently bypassed.
        if identity == image_identity(entry_config(other)):
            if not reason:
                raise ValueError('Same image set already registered as ' + other_name + '; reuse it or document independent VM reason')
            entry['independent_reason'] = required_text(reason, 'reason')
        if not entry.get('run') and not other.get('run') and (
                Path(config['runs_dir']).expanduser().resolve() ==
                Path(entry_config(other)['runs_dir']).expanduser().resolve()):
            raise ValueError('Creation directory already reserved by ' + other_name)
    data['vms'][name] = entry
    return public_entry(name, entry)


def retained_by(data, selected):
    run = Path(selected['run']).resolve()
    disks = {os.path.normcase(str(run / n)) for n in
             ('overlay.qcow2', 'opencore-overlay.qcow2', 'recovery-overlay.qcow2')}
    children = []
    for name, other in data['vms'].items():
        if external.is_entry(other):
            continue
        if not other.get('run') or Path(other['run']).resolve() == run:
            continue
        _, meta = common.load_run(other['run'], other['profile'])
        parents = {os.path.normcase(str(Path(p['path']).resolve()))
                   for key in ('parents', 'opencore_parents', 'recovery_parents') for p in meta.get(key, [])}
        if parents & disks:
            children.append(name)
    return children


def public_entry(name, entry):
    result = {k: v for k, v in entry.items() if k != 'lease'}
    result['name'] = name
    if entry.get('lease'):
        result['owner'] = {k: v for k, v in entry['lease'].items() if k != 'token'}
    return result


def inventory(data):
    images = {}
    records = []
    for name, entry in data['vms'].items():
        result = public_entry(name, entry)
        if external.is_entry(entry):
            records.append(external.inventory(name, entry))
            continue
        try:
            if entry.get('run'):
                run, meta = common.load_run(entry['run'], entry['profile'])
                paths = [p['path'] for k in ('parents', 'opencore_parents', 'recovery_parents')
                         for p in meta.get(k, [])]
                paths += [str(p) for p in run.glob('*.qcow2')]
                paths += [str(p) for p in run.glob('*.fd')]
                paths += [meta['config'][k] for k in ('iso', 'firmware_code', 'firmware_vars')
                          if meta['config'].get(k)]
            else:
                config = entry_config(entry)
                paths = [config[k] for k in ('base_image', 'opencore_image', 'recovery_image',
                                             'firmware_code', 'firmware_vars', 'iso') if config.get(k)]
            for text in set(paths):
                path = Path(text).expanduser().resolve()
                item = images.setdefault(str(path), {'path': str(path), 'vms': [],
                                                     'apparent_bytes': path.stat().st_size})
                if name not in item['vms']:
                    item['vms'].append(name)
        except Exception as error:
            result['inspection_error'] = str(error)
        records.append(result)
    return {'vms': records, 'images': [dict(i, reference_count=len(i['vms'])) for i in images.values()],
            'unique_apparent_bytes': sum(i['apparent_bytes'] for i in images.values()),
            'size_note': 'File lengths, not allocated disk usage or virtual capacity; no automatic cleanup'}


def authorize(entry, token):
    lease = entry.get('lease')
    if not lease or not secrets.compare_digest(lease['token'], token or ''):
        raise RuntimeError('Valid lease token required; claim this VM before changing or operating it')


def dispatch(home, args, external_driver=None, external_reader=None, external_now=None):
    options = {}
    if external_reader is not None: options['reader'] = external_reader
    if external_now is not None: options['now'] = external_now
    selected = external.dispatch_if_external(home, args, sys.modules[__name__], driver=external_driver, **options)
    if selected is not external.NOT_HANDLED:
        return selected
    # Keep the lock across launcher dispatch: release/reclaim cannot race start/shutdown/create.
    with locked(home):
        data = catalog(home)
        if args.action == 'list':
            return dict(inventory(data), pool_home=str(home))
        if args.action == 'register':
            result = register(data, args.name, args.config, args.run, getattr(args, 'reason', None))
        else:
            if args.name not in data['vms']:
                raise ValueError('Unknown VM; list/adopt existing VMs before registering a new slot')
            entry = data['vms'][args.name]
            if args.action == 'claim':
                if entry.get('lease'):
                    raise RuntimeError('VM busy: ' + json.dumps(public_entry(args.name, entry)['owner'], ensure_ascii=False))
                entry['lease'] = {'owner': required_text(args.owner, 'owner'),
                                  'purpose': required_text(args.purpose, 'purpose'),
                                  'token': secrets.token_urlsafe(32), 'claimed_at': time.time()}
                result = dict(public_entry(args.name, entry), token=entry['lease']['token'])
            elif args.action == 'recover':
                if not args.confirm_owner_ended:
                    raise RuntimeError('Verify the previous operator has ended input before deliberate recovery')
                entry['handoff'] = {'summary': required_text(args.reason, 'reason'),
                                    'previous_owner': entry.get('lease', {}).get('owner'),
                                    'recovered_at': time.time()}
                entry.pop('lease', None)
                result = public_entry(args.name, entry)
            elif args.action == 'status':
                result = public_entry(args.name, entry)
                if entry.get('run'):
                    result['vm'] = launcher(entry['profile']).operate('status', entry['run'])
            else:
                authorize(entry, args.token)
                if args.action == 'release':
                    entry['handoff'] = {'summary': required_text(args.summary, 'summary'),
                                        'previous_owner': entry['lease']['owner'], 'released_at': time.time()}
                    del entry['lease']
                    result = public_entry(args.name, entry)
                elif args.action == 'create':
                    if entry.get('run') or entry.get('creation'):
                        raise RuntimeError('VM already exists or creation needs inspection; never retry prepare blindly')
                    config = entry_config(entry)
                    config = launcher(entry['profile']).validate_config(config)
                    if config_digest(config) != entry['config_digest']:
                        raise ValueError('Registered configuration changed; inspect the existing slot instead of creating')
                    # Protect a registered slot against a config edited to another image set.
                    for name, other in data['vms'].items():
                        if (name != args.name and not external.is_entry(other) and image_identity(config) == image_identity(entry_config(other))
                                and not (entry.get('independent_reason') or other.get('independent_reason'))):
                            raise ValueError('Image set already registered as ' + name)
                    root = Path(config['runs_dir']).expanduser().resolve()
                    root.mkdir(parents=True, exist_ok=True)
                    if any(root.iterdir()):
                        raise RuntimeError('runs_dir is not empty; inspect/adopt existing runs, do not create another VM')
                    minimum = args.min_free_gib
                    if not math.isfinite(minimum) or minimum <= 0:
                        raise ValueError('min-free-gib must be positive')
                    if shutil.disk_usage(root).free < minimum * 1024 ** 3:
                        raise RuntimeError('Insufficient free disk; reuse existing VMs or free space explicitly')
                    entry['creation'] = {'reason': required_text(args.reason, 'reason'), 'started_at': time.time()}
                    common.write_json(home / 'catalog.json', data)
                    snapshot = None
                    try:
                        # Use the validated input, not an external file another task can edit.
                        with tempfile.NamedTemporaryFile(dir=home, prefix='create-', suffix='.json', delete=False) as stream:
                            snapshot = Path(stream.name)
                        entry['creation']['config_snapshot'] = str(snapshot)
                        common.write_json(snapshot, config)
                        common.write_json(home / 'catalog.json', data)
                        result = launcher(entry['profile']).prepare(snapshot)
                        entry['run'] = result['run']
                        entry['creation']['completed_at'] = time.time()
                    except Exception as error:
                        entry['creation'].update(error=str(error), inspect_paths=[str(p) for p in root.iterdir()])
                        common.write_json(home / 'catalog.json', data)
                        raise
                    finally:
                        if snapshot is not None:
                            snapshot.unlink(missing_ok=True)
                elif args.action == 'adopt':
                    if entry.get('run'):
                        raise RuntimeError('Slot already has a VM; reuse it')
                    temporary = {'vms': {k: v for k, v in data['vms'].items() if k != args.name}}
                    reason = entry.get('independent_reason') or next(
                        (other.get('independent_reason') for other in temporary['vms'].values()
                         if other.get('image_set') == entry['image_set'] and other.get('independent_reason')), None)
                    register(temporary, args.name, run_path=args.run, reason=reason)
                    adopted = temporary['vms'][args.name]
                    if adopted['image_set'] != entry['image_set']:
                        raise ValueError('Adopted VM uses another image set; inspect the slot and run')
                    entry['run'] = adopted['run']
                    result = public_entry(args.name, entry)
                else:
                    if not entry.get('run'):
                        raise RuntimeError('No VM yet; adopt an existing run or explicitly create with a reason')
                    module = launcher(entry['profile'])
                    if args.action in ('start', 'offline-recover'):
                        children = retained_by(data, entry)
                        if children:
                            raise RuntimeError('VM disks are retained parents; use registered leaf VM: ' + ', '.join(children))
                        if args.action == 'offline-recover':
                            if entry['profile'] != common.GENERIC_PROFILE:
                                raise ValueError('Offline recovery currently supports Windows generic-x86 only')
                            result = module.offline_recover(entry['run'], args.expected_session,
                                {'lab.json': args.expected_lab_sha256, 'started.json': args.expected_started_sha256,
                                 'active-session.json': args.expected_active_session_sha256,
                                 'start.lock': args.expected_start_lock_sha256}, args.reason,
                                catalog_lock_held=True)
                        else:
                            result = module.start(entry['run'])
                    else:
                        result = module.operate(args.action, entry['run'], getattr(args, 'iso', None))
        common.write_json(home / 'catalog.json', data)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    commands.add_parser('list')
    register_cmd = commands.add_parser('register')
    register_cmd.add_argument('--name', required=True)
    source = register_cmd.add_mutually_exclusive_group(required=True)
    source.add_argument('--config')
    source.add_argument('--run')
    register_cmd.add_argument('--reason', help='Why another VM with the same base images is required')
    for name in ('claim', 'release', 'recover', 'status', 'create', 'adopt', 'start', 'shutdown', 'media', 'offline-recover'):
        cmd = commands.add_parser(name)
        cmd.add_argument('--name', required=True)
        if name == 'claim':
            cmd.add_argument('--owner', required=True)
            cmd.add_argument('--purpose', required=True)
        elif name == 'recover':
            cmd.add_argument('--reason', required=True)
            cmd.add_argument('--confirm-owner-ended', action='store_true')
        elif name != 'status':
            cmd.add_argument('--token', required=True)
        if name == 'release':
            cmd.add_argument('--summary', required=True)
        if name == 'create':
            cmd.add_argument('--reason', required=True)
            cmd.add_argument('--min-free-gib', type=float, default=10)
        if name == 'media':
            cmd.add_argument('--iso', required=True)
        if name == 'adopt':
            cmd.add_argument('--run', required=True)
        if name == 'offline-recover':
            cmd.add_argument('--reason', required=True)
            cmd.add_argument('--expected-session', required=True)
            for marker in ('lab', 'started', 'active-session', 'start-lock'):
                cmd.add_argument('--expected-' + marker + '-sha256', required=True)
    ext = commands.add_parser('register-external')
    ext.add_argument('--name', required=True)
    ext.add_argument('--descriptor', required=True)
    ext.add_argument('--descriptor-bytes', required=True, type=int)
    ext.add_argument('--descriptor-sha256', required=True)
    for operation in ('prepare-external', 'action-external', 'bind-external', 'display-external'):
        ext = commands.add_parser(operation)
        ext.add_argument('--name', required=True)
        ext.add_argument('--token', required=True)
        if operation == 'action-external':
            ext.add_argument('--verb', required=True, choices=('observe','continue','powerdown','attach-iso'))
            ext.add_argument('--iso')
            ext.add_argument('--iso-bytes', type=int)
            ext.add_argument('--iso-sha256')
        elif operation == 'bind-external':
            ext.add_argument('--stage', required=True, choices=('finalize','seams','ui'))
            ext.add_argument('--input', required=True)
            ext.add_argument('--input-bytes', required=True, type=int)
            ext.add_argument('--input-sha256', required=True)
        elif operation == 'display-external':
            ext.add_argument('--http-port', required=True, type=int)
    args = parser.parse_args()
    print(json.dumps(dispatch(pool_home(), args), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps({'error': str(error)}, ensure_ascii=False), file=sys.stderr)
        sys.exit(1)
