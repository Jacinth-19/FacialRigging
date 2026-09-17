#!/usr/bin/env python3
"""Executes an Arena agent task file (tools/arena_task.yaml) by translating it into fr_cli
invocations. Only a tiny YAML subset is parsed on purpose (no PyYAML dependency): nested
maps, lists of scalars, `key: value` scalars and `- key: value` list items.

Usage: run_arena_task.py TASK.yaml [--dry-run]
Prints a JSON summary {"files": [...], "log": [...]} on success so an agent can parse it.
"""
import json, os, re, subprocess, sys

def parse_yaml(text):
    """Minimal indentation-based YAML subset parser returning dict/list/str."""
    lines = [l.rstrip() for l in text.splitlines()]
    lines = [re.sub(r'\s+#.*$', '', l) if not l.lstrip().startswith('#') else '' for l in lines]
    lines = [l for l in lines if l.strip()]
    pos = 0
    def indent(l): return len(l) - len(l.lstrip(' '))
    def scalar(v):
        v = v.strip()
        if v[:1] in '"\'' and v[-1:] == v[:1]: return v[1:-1]
        return v
    def parse_block(ind):
        nonlocal pos
        if pos >= len(lines): return None
        if lines[pos].lstrip().startswith('- '): return parse_list(ind)
        return parse_map(ind)
    def parse_map(ind):
        nonlocal pos
        out = {}
        while pos < len(lines) and indent(lines[pos]) == ind and not lines[pos].lstrip().startswith('- '):
            key, _, val = lines[pos].strip().partition(':')
            pos += 1
            if val.strip(): out[key.strip()] = scalar(val)
            else: out[key.strip()] = parse_block(indent(lines[pos])) if pos < len(lines) and indent(lines[pos]) > ind else None
        return out
    def parse_list(ind):
        nonlocal pos
        out = []
        while pos < len(lines) and indent(lines[pos]) == ind and lines[pos].lstrip().startswith('- '):
            item = lines[pos].strip()[2:]
            if ':' in item and not item.startswith(('"', "'")):
                key, _, val = item.partition(':')
                # rewrite "- key: val" into a nested map at indent+2
                lines[pos] = ' ' * (ind + 2) + item
                out.append(parse_map(ind + 2))
            else:
                pos += 1; out.append(scalar(item))
        return out
    return parse_block(indent(lines[0])) if lines else {}

def main():
    if len(sys.argv) < 2: print(__doc__); return 2
    task = parse_yaml(open(sys.argv[1]).read())
    dry = '--dry-run' in sys.argv
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    app = task.get('app') or 'build/fr_cli'
    cmd = [os.path.join(root, app) if not os.path.isabs(app) else app]
    for step in task.get('steps', []):
        for name, arg in (step or {}).items():
            arg = arg if arg is not None else {}
            if name == 'load_model' and arg: cmd += ['--model', arg]
            elif name == 'import_audio' and arg: cmd += ['--audio', arg]
            elif name == 'generate_animation':
                if 'fps' in arg: cmd += ['--fps', str(arg['fps'])]
                if 'intensity' in arg: cmd += ['--intensity', str(arg['intensity'])]
            elif name in ('export', 'export_fbx'):
                cmd += ['--output', arg.get('output', 'out/scene'), '--format', arg.get('format', 'fbx' if name == 'export_fbx' else 'glb')]
            elif name == 'generate_variations':
                for c in arg.get('changes', []) or []: cmd += ['--variation', c]
                if 'count' in arg and not arg.get('changes'): cmd += ['--variations', str(arg['count'])]
    print('$', ' '.join(f'"{c}"' if ' ' in c else c for c in cmd), file=sys.stderr)
    if dry: return 0
    res = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
    files, log = [], []
    for line in res.stdout.splitlines():
        if line.startswith('[fr] '): log.append(line[5:])
        elif line.startswith('  '): files.append(line.strip())
    print(json.dumps({'ok': res.returncode == 0, 'files': files, 'log': log, 'stderr': res.stderr.strip()}, indent=2))
    return res.returncode

if __name__ == '__main__':
    sys.exit(main())
