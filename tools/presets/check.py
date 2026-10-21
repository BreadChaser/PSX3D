#!/usr/bin/env python3
"""Validate the per-game preset files (docs/presets.md).

  python3 tools/presets/check.py [--dir DIR] [--base REF]

Checks every DIR/*.ini (default psx3d/romfs/presets): known keys and values,
section placement, serial format, serials listed in two files, sparse files
(no key equal to the console default), widescreen bundles for warn/off, no
psxclock for games whose clock the core sets itself, and versions: VERSION is
>= every file version, and a file that changed since REF (default: the last
release tag) has a higher version than at REF, with a higher VERSION too.
Exit status 1 on any error.
"""
import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SERIAL = re.compile(r'^[A-Z]{4}[0-9]{5}$')
NAME = re.compile(r'^[a-z0-9]+(-[a-z0-9]+)*\.ini$')
BINDS = 'hide b y select start up down left right a x l1 r1 l2 r2 l3 r3 ff ff_hold rewind'.split()
# key: allowed values (None = number), Old default, New default (None = no default).
OPTIONS = {
    'fast_forward': (['no', 'yes'], 'yes', 'yes'),
    'rewind': (['no', 'yes'], 'no', 'yes'),
    'touch_controls': (['auto', 'on', 'off'], 'auto', 'auto'),
    'l2r2': (['no', 'yes'], 'yes', 'no'),
    'l3r3': (['no', 'yes', 'analog'], 'analog', 'analog'),
    'stick': (['no', 'yes', 'analog'], 'analog', 'no'),
    'frameskip': (['off', 'auto'], 'auto', 'auto'),
    'widescreen': (['hidden', 'on', 'warn', 'off'], 'on', 'on'),
    'psxclock': (None, '57', '57'),
    'auto_floor': (None, '15', '15'),
}
for slot in ('l2', 'r2', 'l3', 'r3', 'x1', 'x2'):
    OPTIONS['touch_' + slot] = (BINDS, None, None)
# Vocabulary and numeric bounds are shared with the C runtime, not duplicated.
SCHEMA = {}
for name, words, lo, hi in re.findall(r'PRESET_KEY\((\w+), "([^"\n]*)", (-?\d+), (\d+)\)',
        (ROOT / 'psx3d/source/preset_schema.def').read_text()):
    SCHEMA[name] = (words.split('|') if words else None, int(lo), int(hi))
    old, new = OPTIONS[name][1:]
    OPTIONS[name] = (SCHEMA[name][0], old, new)
COMMON_ONLY = {'title', 'version', 'serials', 'curated'}


def parse(text):
    """Returns ({section: {key: (value, line)}}, errors), mirroring presets.c."""
    data = {'common': {}, 'old': {}, 'new': {}}
    errors = []
    section = 'common'
    if len(text.splitlines()) > 512:
        errors.append('more than 512 lines')
    for number, raw in enumerate(text.splitlines(keepends=True), 1):
        if len(raw.encode('utf-8')) > 255:
            errors.append(f'line {number}: exceeds 255 bytes including newline')
        if '\x00' in raw or any(ord(c) > 127 for c in raw):
            errors.append(f'line {number}: only ASCII text is supported')
        line = re.split('[;#]', raw, maxsplit=1)[0].strip()
        if not line:
            continue
        if line.startswith('['):
            if line not in ('[common]', '[old]', '[new]'):
                errors.append(f'line {number}: unknown section {line}')
                section = None
            else:
                section = line[1:-1]
            continue
        if '=' not in line:
            errors.append(f'line {number}: expected key = value')
            continue
        key, value = (part.strip() for part in line.split('=', 1))
        if section is None:
            errors.append(f'line {number}: key in an unknown section')
        elif key in data[section]:
            errors.append(f'line {number}: {key} repeated in [{section}]')
        else:
            data[section][key] = (value, number)
    return data, errors


def core_clock_serials():
    """Serials with a core cycle_multiplier override (core/libpcsxcore/database.c)."""
    source = (ROOT / 'core/libpcsxcore/database.c').read_text()
    block = source[source.index('cycle_multiplier_overrides[] ='):]
    block = block[:block.index('};')]
    return set(re.findall(r'"([A-Z]{4}[0-9]{5})"', block))


def git(*args):
    return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True)


def check_file(path, text, bundles, core_clock):
    data, errors = parse(text)
    common = data['common']
    for key in ('title', 'version', 'serials'):
        if key not in common:
            errors.append(f'missing {key}')
    if not common.get('title', ('',))[0]:
        errors.append('title is empty')
    version = common.get('version', ('', 0))[0]
    if 'version' in common and not re.fullmatch(r'[1-9][0-9]{0,5}', version):
        errors.append(f'version must be a positive number, not {version!r}')
    serials = [s.strip() for s in common.get('serials', ('', 0))[0].split(',')]
    if len(serials) > 26:
        errors.append('more than 26 serials')
    if 'serials' in common and not serials:
        errors.append('serials is empty')
    for serial in serials:
        if not SERIAL.match(serial):
            errors.append(f'bad serial {serial!r} (four capital letters, five digits)')
    if len(set(serials)) != len(serials):
        errors.append('a serial is listed twice')
    curated = common.get('curated', ('no', 0))[0]
    if 'curated' in common:
        if curated not in ('yes', 'no'):
            errors.append(f'curated must be yes or no, not {curated!r}')
        elif curated == 'no':
            errors.append('curated = no is the default: leave it out')
    for section, keys in data.items():
        for key, (value, line) in keys.items():
            where = f'line {line}: {key}'
            if '.' in key:
                base, suffix = key.split('.', 1)
                if base not in OPTIONS or suffix not in ('source', 'confidence'):
                    errors.append(f'{where}: unknown evidence key')
                elif not value or (suffix == 'source' and len(value.encode()) > 127):
                    errors.append(f'{where}: source must be 1-127 bytes')
                elif suffix == 'confidence' and value not in ('high', 'medium', 'low', 'unassessed'):
                    errors.append(f'{where}: invalid confidence')
                if base not in keys:
                    errors.append(f'{where}: evidence without an option in the same section')
                continue
            if key in COMMON_ONLY:
                if section != 'common':
                    errors.append(f'{where} belongs at the top, not in [{section}]')
                continue
            if key not in OPTIONS:
                errors.append(f'{where}: unknown key')
                continue
            for suffix in ('source', 'confidence'):
                if key + '.' + suffix not in keys:
                    errors.append(f'{where}: missing {suffix}')
            values, old_default, new_default = OPTIONS[key]
            if values is None:
                if not re.fullmatch(r'[0-9]+', value) or not SCHEMA[key][1] <= int(value) <= SCHEMA[key][2]:
                    errors.append(f'{where}: must be a number {SCHEMA[key][1]}-{SCHEMA[key][2]}')
                    continue
            elif value not in values:
                errors.append(f'{where}: {value!r} is not one of {", ".join(values)}')
                continue
            defaults = {'common': {old_default, new_default} if old_default == new_default else set(),
                        'old': {old_default}, 'new': {new_default}}[section]
            if value in defaults:
                errors.append(f'{where} = {value} is the console default: leave it out')
            if section != 'common' and common.get(key, (None,))[0] == value:
                errors.append(f'{where} repeats the common value')
            if key == 'rewind' and value == 'yes' and section in ('common', 'old'):
                errors.append(f'{where}: Old 3DS cannot rewind; set it in [new]')
            if key == 'widescreen' and value in ('warn', 'off') and not any(s in bundles for s in serials):
                errors.append(f'{where} = {value} needs a widescreen bundle in romfs/widescreen')
            if key == 'psxclock':
                for serial in serials:
                    if serial in core_clock:
                        errors.append(f'{where}: the core sets the clock of {serial}; a preset must not')
    return serials, (int(version) if version.isdigit() else 0), errors


def check_pack_version(data):
    # Exactly one complete ASCII line, using the runtime's shared line bound.
    if len(data) > 255 or not re.fullmatch(rb'[ \t\r\v\f]*[1-9][0-9]{0,5}[ \t\r\v\f]*(?:\n)?', data):
        return 0
    return int(data.strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--dir', default=str(ROOT / 'psx3d/romfs/presets'))
    parser.add_argument('--base', help='git ref to compare versions against (default: last release tag)')
    args = parser.parse_args()
    directory = pathlib.Path(args.dir)
    bundle_dir = ROOT / 'psx3d/romfs/widescreen'
    bundles = {p.name[:9] for p in bundle_dir.glob('*.ws')}
    core_clock = core_clock_serials()
    problems = []
    for bundle in sorted(bundle_dir.glob('*.ws')):
        if re.search(r'^models=', bundle.read_text(), re.M):
            problems.append(f'{bundle.name}: models= moved to the preset files; remove it')

    pack = check_pack_version((directory / 'VERSION').read_bytes()) if (directory / 'VERSION').exists() else 0
    if not pack:
        problems.append('VERSION: missing or not a positive number')
    owner, versions, texts = {}, {}, {}
    if len(list(directory.glob('*.ini'))) > 256:
        problems.append('more than 256 files')
    for path in sorted(directory.iterdir()):
        if path.name == 'VERSION':
            continue
        if not NAME.match(path.name):
            problems.append(f'{path.name}: not a lower-case-hyphenated .ini file name')
            continue
        texts[path.name] = path.read_text()
        serials, version, errors = check_file(path, texts[path.name], bundles, core_clock)
        problems += [f'{path.name}: {e}' for e in errors]
        versions[path.name] = version
        if version > pack:
            problems.append(f'{path.name}: version {version} is above the pack VERSION {pack}')
        for serial in serials:
            if serial in owner:
                problems.append(f'{serial} is in both {owner[serial]} and {path.name}')
            owner.setdefault(serial, path.name)

    if len(owner) > 2048:
        problems.append('more than 2048 serials')

    # Versions against a release: a changed file and the pack must both move up.
    base = args.base or git('describe', '--tags', '--abbrev=0').stdout.strip()
    try:
        relative = directory.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        relative = None
    if base and relative:
        listed = git('ls-tree', '--name-only', f'{base}:{relative}')
        if listed.returncode == 0:
            old = {name: git('show', f'{base}:{relative}/{name}').stdout for name in listed.stdout.split()}
            changed = sorted(set(texts) ^ set(n for n in old if n != 'VERSION') |
                             {n for n in texts if n in old and old[n] != texts[n]})
            for name in changed:
                if name in texts and name in old:
                    _, before, _ = check_file(directory / name, old[name], bundles, core_clock)
                    if versions[name] <= before:
                        problems.append(f'{name}: changed since {base} but version {versions[name]} is not above {before}')
            base_pack = old.get('VERSION', '0').strip()
            if changed and base_pack.isdigit() and pack <= int(base_pack):
                problems.append(f'VERSION: presets changed since {base} ({", ".join(changed)}) but VERSION {pack} '
                                f'is not above {base_pack}')
        else:
            print(f'presets: no preset folder at {base}; version history starts here')

    for problem in problems:
        print('ERROR', problem)
    print(f'presets: {len(texts)} files, {len(owner)} serials, pack VERSION {pack}: '
          f'{"FAIL" if problems else "PASS"}')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
