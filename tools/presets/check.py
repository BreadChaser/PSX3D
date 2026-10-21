#!/usr/bin/env python3
"""Validate the per-game preset files (docs/presets.md).

  python3 tools/presets/check.py [--dir DIR] [--base REF]

Checks every DIR/*.ini (default psx3d/romfs/presets) except defaults.ini: known keys and values,
section placement, serial format, serials listed in two files, sparse files
(no key equal to the console default), widescreen bundles for warn/off, no
psxclock for games whose clock the core sets itself, and versions: VERSION is
>= every file version, and a file that changed since REF (default: the last
release tag) has a higher version than at REF, with a higher VERSION too.

Defaults (docs/settings.md section 6): defaults.ini must equal
tools/presets/defaults-history/<defaults_version>.ini, released history files
never change, and a preset with tested_with = N below the current version must
pin (set) every technical option whose default changed since N, and is
rejected outright when a forced value changed, until it is retested.
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


def read_defaults(path):
    """defaults.ini -> (version, {section: {key: word}}), mirroring presets.c."""
    version, data, section = 0, {}, None
    for raw in pathlib.Path(path).read_text().split('\n'):
        line = re.split('[;#\r]', raw, maxsplit=1)[0].strip()
        if not line:
            continue
        if line.startswith('['):
            section = line[1:-1]
            data.setdefault(section, {})
            continue
        key, value = (x.strip() for x in line.split('=', 1))
        if section is None:
            version = int(value) if key == 'defaults_version' and value.isdigit() else 0
        else:
            data[section][key] = value
    return version, data


DEFAULTS = ROOT / 'psx3d/romfs/presets/defaults.ini'
HISTORY = ROOT / 'tools/presets/defaults-history'
DEFAULTS_VERSION, DEFAULTS_DATA = read_defaults(DEFAULTS)
# Technical settings (layer = tech in docs/settings.md); the rest of [old]/[new] is taste.
TECH = {row.split('|')[2].strip() for row in re.search(r'```settings-menu\n(.*?)```',
        (ROOT / 'docs/settings.md').read_text(), re.S).group(1).strip().split('\n')
        if row.split('|')[8].strip() == 'tech'} - {'-', '>'}
BINDS = 'hide b y select start up down left right a x l1 r1 l2 r2 l3 r3 ff ff_hold rewind'.split()
CLOCK = {'auto': '57'}   # preset numbers for the defaults' auto words


def console_default(key, console, data=None):
    data = data or DEFAULTS_DATA
    value = data.get('forced.' + console, {}).get(key, data.get(console, {}).get(key))
    return CLOCK.get(value, value) if key == 'psxclock' else value


# Vocabulary and numeric bounds are shared with the C runtime, not duplicated.
SCHEMA = {}
OPTIONS = {}
for name, words, lo, hi in re.findall(r'PRESET_KEY\((\w+), "([^"\n]*)", (-?\d+), (\d+)\)',
        (ROOT / 'psx3d/source/preset_schema.def').read_text()):
    SCHEMA[name] = (words.split('|') if words else None, int(lo), int(hi))
    old, new = console_default(name, 'old'), console_default(name, 'new')
    # Capability shorthands keep their documented defaults (docs/presets.md).
    old, new = {'rewind': ('no', 'yes'), 'touch_controls': ('auto', 'auto'), 'l2r2': ('yes', 'no'),
                'l3r3': ('analog', 'analog'), 'stick': ('analog', 'no'),
                'widescreen': ('on', 'on')}.get(name, (old, new))
    OPTIONS[name] = (SCHEMA[name][0], old, new)
RETIRED = {'fast_forward': 'fast forward is always allowed (docs/settings.md); remove it'}
COMMON_ONLY = {'title', 'version', 'serials', 'curated'}


def parse(text):
    """Returns ({section: {key: (value, line)}}, errors), mirroring presets.c."""
    data = {'common': {}, 'old': {}, 'new': {}}
    errors = []
    section = 'common'
    # The C reader splits only on LF, then treats CR as end-of-content.
    # Python splitlines() also splits VT/FF/CR, which would accept different data.
    chunks = text.split('\n')
    lines = [line + '\n' for line in chunks[:-1]]
    if chunks[-1]:
        lines.append(chunks[-1])
    if len(lines) > 512:
        errors.append('more than 512 lines')
    for number, raw in enumerate(lines, 1):
        if len(raw.encode('utf-8')) > 255:
            errors.append(f'line {number}: exceeds 255 bytes including newline')
        if '\x00' in raw or any(ord(c) > 127 for c in raw):
            errors.append(f'line {number}: only ASCII text is supported')
        line = re.split('[;#\r\n]', raw, maxsplit=1)[0].strip(' \t\r\n\v\f')
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
        key, value = (part.strip(' \t\r\n\v\f') for part in line.split('=', 1))
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
    serials = [s.strip(' \t\r\n\v\f') for s in common.get('serials', ('', 0))[0].split(',')]
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
            if key in RETIRED:
                errors.append(f'{where}: {RETIRED[key]}')
                continue
            if key not in OPTIONS:
                errors.append(f'{where}: unknown key')
                continue
            for suffix in ('source', 'confidence'):
                if key + '.' + suffix not in keys:
                    errors.append(f'{where}: missing {suffix}')
            values, old_default, new_default = OPTIONS[key]
            if values is None:
                if not re.fullmatch(r'[0-9]{1,6}', value) or not SCHEMA[key][1] <= int(value) <= SCHEMA[key][2]:
                    errors.append(f'{where}: must be a number {SCHEMA[key][1]}-{SCHEMA[key][2]}')
                    continue
            elif value not in values:
                errors.append(f'{where}: {value!r} is not one of {", ".join(values)}')
                continue
            defaults = {'common': {old_default, new_default} if old_default == new_default else set(),
                        'old': {old_default}, 'new': {new_default}}[section]
            pin = 'pinned at defaults' in keys.get(key + '.source', ('',))[0]
            if value in defaults and not pin:
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
    errors += check_tested(data)
    return serials, (int(version) if version.isdigit() else 0), errors


def history(version):
    path = HISTORY / f'{version}.ini'
    return read_defaults(path)[1] if path.exists() else None


def check_tested(data):
    """Section 6.3: a tested preset pins or retests every changed default."""
    errors = []
    for console in ('old', 'new'):
        value = data[console].get('tested_with', data['common'].get('tested_with', (None, 0)))[0]
        if value is None:
            continue
        if not value.isdigit():
            continue   # reported as a bad number by the option check
        tested = int(value)
        if tested > DEFAULTS_VERSION:
            errors.append(f'tested_with = {tested} in [{console}] is above defaults_version {DEFAULTS_VERSION}')
            continue
        then = history(tested)
        if then is None:
            errors.append(f'tested_with = {tested}: no tools/presets/defaults-history/{tested}.ini')
            continue
        if tested == DEFAULTS_VERSION:
            continue
        for key in sorted(set(then.get(console, {})) | set(DEFAULTS_DATA.get(console, {}))):
            old, new = then.get(console, {}).get(key), DEFAULTS_DATA.get(console, {}).get(key)
            if old == new or key not in TECH:
                continue   # taste defaults never fail (section 6.3, rule 4)
            if key not in data[console] and key not in data['common']:
                errors.append(f'[{console}] tested_with {tested}: default {key} changed {old} -> {new}; '
                              f'pin it ({key} = {old}, {key}.source = pinned at defaults {tested}) or retest')
        for key in sorted(set(then.get('forced.' + console, {})) | set(DEFAULTS_DATA.get('forced.' + console, {}))):
            old, new = then.get('forced.' + console, {}).get(key), DEFAULTS_DATA.get('forced.' + console, {}).get(key)
            if old != new:
                errors.append(f'[{console}] tested_with {tested}: forced {key} changed {old} -> {new}; '
                              f'forced values cannot be pinned, retest (tested_with = {DEFAULTS_VERSION})')
    return errors


def check_defaults(base):
    """defaults.ini equals its history file; released history files are frozen."""
    problems = []
    if DEFAULTS_VERSION <= 0:
        return ['defaults.ini: missing or bad defaults_version']
    archived = HISTORY / f'{DEFAULTS_VERSION}.ini'
    if not archived.exists():
        problems.append(f'defaults.ini: defaults_version {DEFAULTS_VERSION} has no {archived.relative_to(ROOT)}')
    elif archived.read_bytes() != DEFAULTS.read_bytes():
        problems.append(f'defaults.ini differs from {archived.relative_to(ROOT)}: a changed default needs '
                        f'a new defaults_version and history file')
    for console in ('old', 'new'):
        if set(DEFAULTS_DATA.get(console, {})) != set(DEFAULTS_DATA.get('old', {})):
            problems.append(f'defaults.ini: [old] and [{console}] list different settings')
        if set(DEFAULTS_DATA.get('forced.' + console, {})) != set(DEFAULTS_DATA.get('forced.old', {})):
            problems.append(f'defaults.ini: [forced.old] and [forced.{console}] list different keys')
    if base:
        listed = git('ls-tree', '--name-only', f'{base}:tools/presets/defaults-history')
        for name in listed.stdout.split() if listed.returncode == 0 else []:
            before = git('show', f'{base}:tools/presets/defaults-history/{name}').stdout
            if not (HISTORY / name).exists() or (HISTORY / name).read_text() != before:
                problems.append(f'defaults-history/{name} changed after release {base}; never edit a released version')
    return problems


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
        if path.name in ('VERSION', 'defaults.ini'):
            continue
        if not NAME.match(path.name):
            problems.append(f'{path.name}: not a lower-case-hyphenated .ini file name')
            continue
        texts[path.name] = path.read_bytes().decode('utf-8', errors='replace')
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
    problems += check_defaults(base)
    try:
        relative = directory.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        relative = None
    if base and relative:
        listed = git('ls-tree', '--name-only', f'{base}:{relative}')
        if listed.returncode == 0:
            old = {name: git('show', f'{base}:{relative}/{name}').stdout for name in listed.stdout.split()}
            changed = sorted(set(texts) ^ set(n for n in old if n not in ('VERSION', 'defaults.ini')) |
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
