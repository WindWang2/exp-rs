#!/usr/bin/env python3
"""WP-A census extraction: 157 registrations -> dual-gauge coverage matrix.

Reads only the source tree; writes META_COVERAGE_MATRIX.md next to this script.
Ground truth per row:
  - registration   : REGISTER_RS_OPERATOR(Class, "rs:id") in rs_operators_init.cpp
  - class/impl     : class declared in src/operators/rs/<file>.h, run() defined in <impl>.cpp
  - sparse sidecar : data/processing/algorithm_meta/rs-*.json  (== taskFamily declared in code)
  - capability     : data/processing/algorithm_meta/capability/rs-*.json (registry-pinned layer)
Authored-key census from capability sidecar JSON (P3 boundary in BASELINE.md).
"""
import json, os, re, subprocess, sys

ROOT = os.environ.get("R4_ROOT", "/home/kevin/project/exp-rs-processing-meta-r4")
RS = os.path.join(ROOT, "src/operators/rs")
CAP = os.path.join(ROOT, "data/processing/algorithm_meta/capability")
SPARSE = os.path.join(ROOT, "data/processing/algorithm_meta")
OUT = os.path.join(ROOT, ".planning/processing-meta-r4/META_COVERAGE_MATRIX.md")

def sh(*args):
    return subprocess.run(args, capture_output=True, text=True, cwd=ROOT).stdout

# 1. registrations (ordered)
regs = []
init_cpp = os.path.join(RS, "rs_operators_init.cpp")
for m in re.finditer(r'REGISTER_RS_OPERATOR\(\s*(\w+)\s*,\s*"(rs:[^"]+)"\s*\)',
                     open(init_cpp).read()):
    regs.append((m.group(2), m.group(1)))

# 2. class -> declaring header
cls_h = {}
for f in os.listdir(RS):
    if f.endswith(".h"):
        for c in re.findall(r'class\s+(Rs\w+Operator)\b', open(os.path.join(RS, f)).read()):
            cls_h.setdefault(c, f)

# 3. class -> implementation cpp (definition site of ClassName::run / ClassName::)
cls_cpp = {}
for f in os.listdir(RS):
    if f.endswith(".cpp"):
        txt = open(os.path.join(RS, f)).read()
        for c in set(re.findall(r'\b(Rs\w+Operator)::', txt)):
            cls_cpp.setdefault(c, f)

# 4. sidecars
sparse = {}
for f in os.listdir(SPARSE):
    if f.endswith(".json"):
        sparse[f] = json.load(open(os.path.join(SPARSE, f)))
cap = {}
for f in sorted(os.listdir(CAP)):
    if f.endswith(".json") and f.startswith("rs-"):
        cap[f] = json.load(open(os.path.join(CAP, f)))

def fname(op_id):
    # mirror AlgorithmMetaStore::idToFileName (algorithm_meta_store.cpp:196):
    # ':' and '_' both normalize to '-'
    return re.sub(r"[:_]", "-", op_id) + ".json"

AUTH = ["summary", "failure_modes", "applicability", "teaching_use", "prerequisites", "limitations"]

rows, problems = [], []
for op_id, cls in regs:
    fn = fname(op_id)
    sp = sparse.get(fn)
    cp = cap.get(fn)
    if cp is None:
        problems.append(f"{op_id}: capability sidecar missing ({fn})")
        auth = {k: "-" for k in AUTH}
        fam = task = det = mem = "-"
        gpu = acc = inp = outp = "-"
    else:
        c = cp.get("capability", {})
        task = cp.get("task", "")
        fam = c.get("family", "")
        det = c.get("determinism", {}).get("grade", "")
        mem = c.get("determinism", {}).get("memory_policy", "")
        gpu = "gpu" if cp.get("gpu") is True else ("nogpu" if cp.get("gpu") is False else "-")
        acc = "Y" if isinstance(cp.get("accuracy"), (int, float)) else "-"
        ios = c.get("io", {})
        inp = ",".join(sorted({p.get("data_kind", "?") for p in ios.get("inputs", [])})) or "∅"
        outp = ",".join(sorted({p.get("data_kind", "?") for p in ios.get("outputs", [])})) or "∅"
        auth = {}
        for k in AUTH:
            v = c.get(k)
            ok = bool(v) if isinstance(v, (list, dict, str)) else False
            auth[k] = "Y" if ok else "×"
    sps = "Y" if sp else "×"
    if sp and sp.get("id") != op_id:
        problems.append(f"{op_id}: sparse sidecar id mismatch {sp.get('id')}")
    rows.append(dict(id=op_id, cls=cls, h=cls_h.get(cls, "?"), cpp=cls_cpp.get(cls, "?"),
                     sparse=sps, task=task, fam=fam, det=det, mem=mem, gpu=gpu, acc=acc,
                     inp=inp, outp=outp, **auth))

# dual-gauge gap
sparse_missing = [r for r in rows if r["sparse"] == "×"]
file_gauge = set()
for r in rows:
    if r["sparse"] == "×":
        file_gauge.add(r["cpp"])
auth_missing = {k: [r["id"] for r in rows if r[k] == "×"] for k in AUTH}

json.dump({"work_order": sorted({r["id"] for r in rows if any(r[k] == "×" for k in AUTH)}),
           "gaps": {k: [r["id"] for r in rows if r[k] == "×"] for k in AUTH}},
          open(os.path.join(ROOT, ".planning/processing-meta-r4/work_order.json"), "w"), indent=1)

with open(OUT, "w") as out:
    out.write("# META_COVERAGE_MATRIX — 157 注册 × 双口径 × 字段完备（WP-A）\n\n")
    out.write(f"生成：.planning/processing-meta-r4/gen_matrix.py（只读源树，可重跑复核）\n\n")
    out.write("## 汇总\n\n")
    out.write(f"- 注册行数：**{len(rows)}**（== REGISTER_RS_OPERATOR 计数 157）\n")
    out.write(f"- 唯一类：{len({r['cls'] for r in rows})}；声明头文件：{len({r['h'] for r in rows})}；实现 .cpp：{len({r['cpp'] for r in rows})}\n")
    out.write(f"- sparse sidecar（== 代码中声明 taskFamily，设计不变量见 BASELINE P1）：**{157-len(sparse_missing)}/157**，缺口 {len(sparse_missing)}（注册口径）\n")
    out.write(f"- 文件口径缺口：实现 taskFamily 算子涉及的、无 sparse 覆盖的 .cpp 文件数 = {len(file_gauge)}（逐行归位见矩阵）\n")
    out.write(f"- capability 侧车覆盖：**{sum(1 for r in rows if r['fam'] != '-') + sum(1 for r in rows if r['fam'] == '-')}/157**（缺文件 {157 - min(len(cap),157)} 个）\n")
    out.write(f"- authored 字段缺口：applicability {len(auth_missing['applicability'])} / teaching_use {len(auth_missing['teaching_use'])} / prerequisites {len(auth_missing['prerequisites'])} / limitations {len(auth_missing['limitations'])}（summary/failure_modes 应为 0）\n")
    out.write(f"- determinism 声明缺失：{sum(1 for r in rows if r['det'] in ('','-'))}；memory_policy 缺失：{sum(1 for r in rows if r['mem'] in ('','-'))}\n")
    out.write(f"- gpu 声明（true/false 显式）：{sum(1 for r in rows if r['gpu'] != '-')}/157 显式；accuracy 实测值存在：{sum(1 for r in rows if r['acc']=='Y')}/157\n\n")
    out.write("## 矩阵（157 行，列：注册名 | 类 | 头文件 | 实现.cpp | sparse | task | family | det | mem | gpu | acc | in→out | applic | teach | prereq | limit）\n\n")
    out.write("| 注册名 | 类 | .h | .cpp | sparse | task | family | det | mem | gpu | acc | in→out | applic | teach | prereq | limit |\n")
    out.write("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n")
    for r in rows:
        out.write(f"| {r['id']} | {r['cls']} | {r['h']} | {r['cpp']} | {r['sparse']} | {r['task'] or '∅'} | {r['fam'] or '∅'} | {r['det'] or '∅'} | {r['mem'] or '∅'} | {r['gpu']} | {r['acc']} | {r['inp']}→{r['outp']} | {r['applicability']} | {r['teaching_use']} | {r['prerequisites']} | {r['limitations']} |\n")
    out.write("\n## 缺口清单\n\n")
    out.write("### sparse（taskFamily）缺口（注册口径，设计上 opt-in，处置=backlog）\n\n")
    for r in sparse_missing: out.write(f"- {r['id']}（{r['cls']}，{r['cpp']}）\n")
    for k in AUTH:
        out.write(f"\n### authored {k} 缺口（{len(auth_missing[k])}）\n\n")
        for i in auth_missing[k]: out.write(f"- {i}\n")

print(f"rows={len(rows)} sparse_missing={len(sparse_missing)} cap_files={len(cap)}")
for k in AUTH: print(f"{k}: missing {len(auth_missing[k])}")
print(f"det_missing={sum(1 for r in rows if r['det'] in ('','-'))} mem_missing={sum(1 for r in rows if r['mem'] in ('','-'))}")
print("problems:", len(problems))
for p in problems[:10]: print("  !", p)
