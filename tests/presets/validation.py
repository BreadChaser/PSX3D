#!/usr/bin/env python3
"""Malformed-file corpus exercised by both the real C loader and pack checker."""
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('checker', ROOT/'tools/presets/check.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)
BASE = 'title = Fixture\nversion = 2\nserials = TEST00001\n'
OPT = 'fast_forward = no\nfast_forward.source = Unassessed host fixture\nfast_forward.confidence = unassessed\n'
cases = {
    'valid': (BASE+'[old]\n'+OPT, True),
    'missing-title': (BASE.replace('title = Fixture\n', '')+OPT, False),
    'empty-title': (BASE.replace('Fixture', '')+OPT, False),
    'missing-version': (BASE.replace('version = 2\n', '')+OPT, False),
    'missing-serials': (BASE.replace('serials = TEST00001\n', '')+OPT, False),
    'bad-serial': (BASE.replace('TEST00001', '123456789')+OPT, False),
    'lowercase-serial': (BASE.replace('TEST00001', 'test00001')+OPT, False),
    'empty-serial': (BASE.replace('TEST00001', 'TEST00001,')+OPT, False),
    'repeated-serial': (BASE.replace('TEST00001', 'TEST00001, TEST00001')+OPT, False),
    'duplicate-key': (BASE+OPT+OPT, False),
    'duplicate-title': (BASE+'title = Again\n'+OPT, False),
    'bad-section': (BASE+'[weird]\n'+OPT, False),
    'metadata-in-section': (BASE+'[old]\nversion = 2\n'+OPT, False),
    'unknown-option': (BASE+OPT+'nonsense = yes\n', False),
    'bad-value': (BASE+OPT.replace('= no\n', '= huh\n'), False),
    'missing-evidence': (BASE+'fast_forward = no\n', False),
    'orphan-evidence': (BASE+'fast_forward.source = fixture\n', False),
    'bad-confidence': (BASE+OPT.replace('unassessed\n', 'proven\n'), False),
    'nul-byte': (BASE+OPT+';\x00hidden\n', False),
    'overlong-comment': (BASE+OPT+';'+'x'*254+'\n', False),
    'too-many-lines': (BASE+OPT+'\n'*512, False),
    'valid-floor': (BASE+OPT+'auto_floor = 12\nauto_floor.source = host only\nauto_floor.confidence = unassessed\n', True),
    'low-floor': (BASE+OPT+'auto_floor = 0\nauto_floor.source = host\nauto_floor.confidence = low\n', False),
    'high-floor': (BASE+OPT+'auto_floor = 61\nauto_floor.source = host\nauto_floor.confidence = low\n', False),
    'final-line-no-newline': ((BASE+OPT).rstrip('\n'), True),
}
with tempfile.TemporaryDirectory() as td:
    pack = pathlib.Path(td)
    (pack/'VERSION').write_text('2\n')
    for name, (data, expected) in cases.items():
        (pack/'fixture.ini').write_text(data)
        errors = checker.check_file(pack/'fixture.ini', data, set(), set())[2]
        assert (not errors) == expected, (name, errors)
        subprocess.run([sys.argv[1], '--validate', str(pack), 'yes' if expected else 'no'], check=True,
                       stdout=subprocess.DEVNULL)
print(f'PASS: {len(cases)} file validation cases agree between runtime and checker')
