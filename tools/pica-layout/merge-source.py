#!/usr/bin/env python3
"""Prepare a merge commit using real core sources; never edit either worktree.

Commit the performance worker's pending work first, then run:
  python3 tools/pica-layout/merge-source.py --target t3/o3ds60
Review the resulting branch before merging it into the worker.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATCH = 'patches/0002-psx3d-widescreen-and-stereo-depth.patch'
CORE_FILES = ['Makefile', 'plugins/gpu_pica/gpu_pica.c',
              'plugins/gpulib/gpu.c', 'plugins/gpulib/pica_layout.h']

def run(args, *, cwd=ROOT, data=None, env=None, ok=(0,)):
    p = subprocess.run(args, cwd=cwd, input=data, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, env=env)
    if p.returncode not in ok:
        raise RuntimeError(f'{args}: {p.stderr.decode()} {p.stdout.decode()}')
    return p.stdout

def git(*args, **kwargs):
    return run(['git', *args], **kwargs)

def show(ref, path):
    return git('show', f'{ref}:{path}')

def snapshot(path, ref, upstream):
    path.mkdir()
    git('init', '-q', cwd=path)
    patch = show(ref, PATCH)
    for name in CORE_FILES:
        src = run(['git', '-C', str(ROOT / 'core'), 'show', f'{upstream}:{name}'], ok=(0,128))
        if src:
            dest = path / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(src)
    args = ['git', 'apply'] + [f'--include={n}' for n in CORE_FILES]
    run(args, cwd=path, data=patch)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--target', default='t3/o3ds60')
    ap.add_argument('--candidate', default='t3/pica-layout')
    ap.add_argument('--branch', default='t3/pica-layout-on-o3ds60')
    ap.add_argument('--output', default='/home/tony/Development/PS1_3ds/devtools/pica-layout/integration')
    a = ap.parse_args()
    target = git('rev-parse', a.target).decode().strip()
    candidate = git('rev-parse', a.candidate).decode().strip()
    base = git('merge-base', target, candidate).decode().strip()
    upstream = git('rev-parse', f'{target}:core').decode().strip()
    for ref in (base, candidate):
        assert git('rev-parse', f'{ref}:core').decode().strip() == upstream, 'core upstream differs'
    merge = run(['git', 'merge-tree', '--write-tree', target, candidate], ok=(0,1)).decode()
    tree = merge.splitlines()[0]
    conflicts = {line.split('\t',1)[1] for line in merge.splitlines() if '\t' in line}
    assert conflicts <= {PATCH, 'psx3d/source/devcontrol.c'}, conflicts
    output = Path(a.output)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='source-', dir=output) as temp:
        temp = Path(temp)
        for label, ref in [('base',base), ('target',target), ('candidate',candidate)]:
            snapshot(temp / label, ref, upstream)
        target_exists = {n for n in CORE_FILES if (temp / 'target' / n).exists()}
        merged = {}
        for name in CORE_FILES:
            paths = []
            for label in ('target','base','candidate'):
                p = temp / label / name
                if not p.exists():
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_bytes(b'')
                paths.append(str(p))
            merged[name] = run(['git', 'merge-file', '-p', *paths])
        source = temp / 'target'
        for name in set(CORE_FILES) - target_exists:
            (source / name).unlink(missing_ok=True)
        git('add','.',cwd=source)
        source_tree = git('write-tree',cwd=source).decode().strip()
        for name,data in merged.items():
            (source / name).write_bytes(data)
        git('add','-N','.',cwd=source)
        source_delta = git('-c','diff.mnemonicPrefix=false','-c','diff.noprefix=false',
                           'diff',source_tree,cwd=source)
        (output/'core-source.delta').write_bytes(source_delta)
        core = temp / 'core'
        core.mkdir()
        archive = git('-C', str(ROOT / 'core'), 'archive', upstream)
        run(['tar', '-xf', '-', '-C', str(core)], data=archive)
        git('init','-q',cwd=core)
        git('add','.',cwd=core)
        upstream_tree = git('write-tree',cwd=core).decode().strip()
        run(['git','apply'],cwd=core,data=show(target,PATCH))
        for name,data in merged.items():
            dest = core / name
            dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_bytes(data)
        git('add','-N','.',cwd=core)
        patch = git('-c','diff.mnemonicPrefix=false','-c','diff.noprefix=false',
                    '-c','core.abbrev=7','diff',upstream_tree,'--','Makefile','Makefile.libretro',
                    'frontend/libretro.c','libpcsxcore','plugins',cwd=core)
        run(['git','apply','--reverse','--check'],cwd=core,data=patch)
        (output / 'merged-core.patch').write_bytes(patch)
        # Only the original layout build identity conflicted. Retain every
        # target status field and its build number, then append the layout.
        control = show(target,'psx3d/source/devcontrol.c').decode()
        control,n = re.subn(r'(\\"build\\":\\"[^"\n]*?)(\\",\\"rtfs)',
                           r'\1-pica-" PICA_LAYOUT_NAME "\2',control)
        assert n == 1, 'build identity format changed; inspect before integration'
        index = temp / 'index'
        env = dict(os.environ,GIT_INDEX_FILE=str(index))
        git('read-tree',tree,env=env)
        for path,data in [(PATCH,patch),('psx3d/source/devcontrol.c',control.encode())]:
            blob = git('hash-object','-w','--stdin',data=data).decode().strip()
            git('update-index','--add','--cacheinfo',f'100644,{blob},{path}',env=env)
        finaltree = git('write-tree',env=env).decode().strip()
        commit = git('commit-tree',finaltree,'-p',target,'-p',candidate,
                     data=b'Merge renderer layout through actual core sources\n').decode().strip()
        old = run(['git','rev-parse','--verify',f'refs/heads/{a.branch}'],ok=(0,128)).decode().strip()
        git('update-ref',f'refs/heads/{a.branch}',commit,old or '0'*40)
        detail = f'target {target}\ncandidate {candidate}\nbase {base}\nmerge {commit}\nbranch {a.branch}\n'
        (output/'identity.txt').write_text(detail)
        (output/'merge-tree.txt').write_text(merge)
        print(detail)
        print('No worktree was changed. Review, then merge this branch only after committing worker work.')

if __name__ == '__main__':
    main()
