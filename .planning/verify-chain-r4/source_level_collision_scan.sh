#!/usr/bin/env bash
# source_level_collision_scan.sh — full-build-criterion disclosure scanner (R3, review P2-2).
#
# Source-level approximation of the census REVERSE rule over ALL registered
# targets (not just the built closure). For every target registered WITHOUT
# a TEST_PREFIX (through any channel: sicnu_add_*, add_executable,
# qt_add_executable, foreach-resolved), scan its TEST_CASE names for the D6
# domain regex after stripping a trailing bracketed Catch2 tag segment.
# This is the number the PR discloses as "would collide with the D6 gate on
# a full-build criterion" — the closure criterion is enforced by
# chain_gate_census.sh REVERSE; this scanner is the disclosure twin.
#
# Usage: source_level_collision_scan.sh   (repo root; no build dir needed)
set -u
cd "$(dirname "$0")/../.." || exit 1

python3 - <<'PYEOF'
import re, os

cm = open('tests/CMakeLists.txt').read()
kw = re.compile(r'verif|grader|preflight|suitab|science|evidence', re.I)

# Resolve foreach variables so foreach-registered targets are visible.
vars = {}
for m in re.finditer(r'foreach\((\w+)\s*\n(.*?)\)\n(.*?)endforeach\s*\(\)', cm, re.S):
    var, items, body = m.group(1), m.group(2), m.group(3)
    names = [x.strip() for x in items.strip().split('\n') if x.strip()]
    vars[var] = (names, body)

blocks = {}  # name -> {'prefixed': bool, 'srcs': list|None}
def reg(name, args, kind):
    prefixed = 'TEST_PREFIX' in args
    srcs = re.findall(r'[\w./]+\.(?:cpp|h)', args) if kind == 'exe' else None
    e = blocks.setdefault(name, {'prefixed': prefixed, 'srcs': srcs})
    e['prefixed'] = e['prefixed'] or prefixed
    if srcs: e['srcs'] = srcs

for m in re.finditer(r'sicnu_add_test\(\s*([A-Za-z0-9_]+)([^)]*)\)', cm):
    reg(m.group(1), m.group(2), 'add')
for m in re.finditer(r'sicnu_add_sdk_test\(\s*([A-Za-z0-9_]+)([^)]*)\)', cm):
    reg(m.group(1), m.group(2), 'add')
for m in re.finditer(r'(?:qt_add_executable|add_executable)\(\s*(test_[A-Za-z0-9_]+)([^)]*)\)', cm, re.S):
    reg(m.group(1), m.group(2), 'exe')
for var, (names, body) in vars.items():
    for n in names:
        if not n.startswith('test_'): continue
        expanded = body.replace('${%s}' % var, n)
        m = re.search(r'(?:qt_add_executable|add_executable)\(\s*%s([^)]*)\)' % re.escape(n), expanded, re.S)
        if m: reg(n, m.group(1), 'exe')
        for dm in re.finditer(r'sicnu_discover_tests\(\s*%s([^)]*)\)' % re.escape(n), expanded):
            if 'TEST_PREFIX' in dm.group(1): reg(n, 'TEST_PREFIX x', 'add')

discover_prefixed = {}
for m in re.finditer(r'sicnu_discover_tests\(\s*([A-Za-z0-9_]+)([^)]*)\)', cm, re.S):
    discover_prefixed[m.group(1)] = 'TEST_PREFIX' in m.group(2)

total_cases, targets_hit = 0, 0
for name, e in sorted(blocks.items()):
    if e['prefixed']: continue
    if discover_prefixed.get(name): continue
    files = e['srcs'] if e['srcs'] else [f'tests/{name}.cpp']
    n = 0
    for f in files:
        if not os.path.exists(f): continue
        for cm2 in re.finditer(r'TEST_CASE\(\s*"([^"]*)"', open(f, errors='replace').read()):
            case = re.sub(r'\s*\[[^\]]*\]\s*$', '', cm2.group(1))  # strip trailing tag segment
            if kw.search(case): n += 1
    if n:
        targets_hit += 1
        total_cases += n
        print(f'{name}: {n}')
print(f'TOTAL: {total_cases} collision cases in {targets_hit} unprefixed targets (source-level, full-build criterion)')
PYEOF
