#!/usr/bin/env python3
"""Validate PSX3D's legacy CIA version mapping and the version makerom actually wrote."""
import argparse
from pathlib import Path
import re
import struct
import sys


def encode(version):
    match = re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version)
    if not match:
        raise ValueError('PSX3D_VERSION must be major.minor.patch (nonnegative decimal integers)')
    major, minor, patch = map(int, match.groups())
    # Keep the released mapping: (major*16+minor)*1024 + patch+3.
    # A carry would alias a different semantic version, even with fixed makerom.
    if minor > 15:
        raise ValueError('minor must be 0..15: minor 16 aliases the next major')
    if patch > 12:
        raise ValueError('patch must be 0..12: patch+3 must fit the 4-bit CIA micro field; bump minor')
    if major * 16 + minor > 63:
        raise ValueError('major*16+minor exceeds the 6-bit CIA major field (max 63)')
    return (major * 16 + minor) * 1024 + patch + 3


def header_version(path):
    matches = re.findall(r'^#define\s+PSX3D_VERSION\s+"([^"\n]+)"', Path(path).read_text(), re.M)
    if len(matches) != 1:
        raise ValueError('expected exactly one PSX3D_VERSION in ' + str(path))
    return matches[0]


def counter(value):
    if not re.fullmatch(r'0|[1-9][0-9]*', value) or not 0 <= int(value) <= 65535:
        raise ValueError('VERSION must be an integer in 0..65535; refusing title-version truncation')
    return int(value)


def cia_version(path):
    # CIA sections are 64-byte aligned. Parse the signed TMD, not makerom stdout.
    align = lambda n: (n + 63) & ~63
    with Path(path).open('rb') as f:
        header = f.read(32)
        if len(header) != 32:
            raise ValueError('truncated CIA header')
        size, kind, version, cert, ticket, tmd_size = struct.unpack_from('<IHHIII', header)
        if size < 32 or size > 0x100000 or kind != 0 or version != 0:
            raise ValueError('unsupported CIA header')
        tmd = align(size) + align(cert) + align(ticket)
        f.seek(tmd)
        sig = f.read(4)
        if len(sig) != 4:
            raise ValueError('missing TMD signature')
        sig_size = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80,
                    0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}.get(int.from_bytes(sig, 'big'))
        if sig_size is None or tmd_size < sig_size + 0x9e:
            raise ValueError('invalid/truncated TMD')
        f.seek(tmd + sig_size + 0x9c)
        raw = f.read(2)
        if len(raw) != 2:
            raise ValueError('truncated TMD title version')
        return int.from_bytes(raw, 'big')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    e = sub.add_parser('encode'); e.add_argument('header')
    c = sub.add_parser('check'); c.add_argument('header'); c.add_argument('value')
    v = sub.add_parser('verify-cia'); v.add_argument('cia'); v.add_argument('value')
    a = p.parse_args()
    try:
        if a.command == 'encode':
            print(encode(header_version(a.header)))
        elif a.command == 'check':
            encode(header_version(a.header))
            counter(a.value)
        else:
            want = counter(a.value)
            actual = cia_version(a.cia)
            if actual != want:
                raise ValueError(f'makerom wrote title version {actual}, requested {want}. '
                                 'The stock makerom drops bits 4..9 (e.g. 5136 becomes 5120); '
                                 'use the pinned tool with tools/build/makerom-version-fix.patch')
            print(f'CIA title version verified: {actual}')
    except (OSError, ValueError, struct.error) as error:
        sys.exit('CIA version error: ' + str(error))

if __name__ == '__main__':
    main()
