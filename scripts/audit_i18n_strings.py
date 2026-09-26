#!/usr/bin/env python3
"""audit_i18n_strings.py — single-source mechanical classifier for user-visible
string literals (Track 17 R4, WP-A/WP-F).

Classification rules (single point of truth, aligned with tests/test_i18n.cpp
"[i18n][sweep]" whose 12-API unwrapped-literal regex is the pre-existing
mechanical rule; this script extends the same API set and adds Chinese-literal
and format/concat detection):

  tr_wrapped_en literal wrapped by tr( / QCoreApplication::translate( /
               QObject::tr( / QT_TRANSLATE_NOOP(, English source — in contract
  tr_zh_source tr()-wrapped but the source literal itself contains Han chars
               (violates the English-source convention, docs/i18n.md) —
               migration targets: English source + zh translation backfill
  hard_zh      user-visible API receives a bare literal containing Han chars
  hard_en      user-visible API receives a bare ASCII literal
  fmt_concat   user-visible API argument concatenates a bare literal with
               non-literal parts (+, QString::number, .arg) without tr()
  nonui_zh     Chinese literal whose statement does not reach a user-visible
               API (data faces, agent/CLI text, JSON payloads) — exemption
               candidates, each recorded with line number for review
  log_zh       Chinese literal inside a qDebug/qWarning/qCritical/qInfo/QLogging
               statement — exempt by rule D1/D2
  comment_zh   Han chars inside comments — exempt (not strings)

Exit code 0 always for the audit itself; use --gate with baseline JSON to
enforce no-regression (WP-F).  --selftest runs constructive pos/neg cases.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Core set: identical to tests/test_i18n.cpp sweep regex (do not drift).
CORE_APIS = [
    "setText", "setWindowTitle", "setTitle", "addItem", "insertItem",
    "addAction", "setToolTip", "setStatusTip", "setPlaceholderText",
    "showMessage", "setLabelText", "setTabText",
]
# Extension set: user-visible sinks that pass the same bare-literal shape.
EXT_APIS = [
    "addItems", "setItemText", "setCheckableText", "setDescription",
    "setSubtitle", "setStatusMessage", "setNotificationText", "setButtonText",
    "addTab", "insertTab", "setSectionText", "setFooterText", "setHeaderText",
]
TR_WRAPPER_RE = re.compile(
    r"\b(?:tr|QObject::tr|QCoreApplication::translate|translate|QT_TRANSLATE_NOOP|QT_TR_NOOP)\s*\(\s*(?:\"|QT_TRANSLATE_NOOP)"
)
USER_VISIBLE_RE = re.compile(
    r"\b(?:%s)\s*\(" % "|".join(re.escape(a) for a in CORE_APIS + EXT_APIS)
)
LOG_RE = re.compile(
    r"\b(?:qDebug|qWarning|qCritical|qInfo|qCDebug|qCWarning|qCCritical|QLoggingCategory)\b"
)
MSGBOX_RE = re.compile(
    r"\bQMessageBox::(?:information|warning|critical|question|about)\s*\("
)
STRING_LIT_RE = re.compile(r'"(?:[^"\\\n]|\\.)*"')
HAN_RE = re.compile(r"[\u4e00-\u9fff]")
LINE_COMMENT_RE = re.compile(r"//[^\n]*")
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)


def strip_comments(text: str) -> tuple[str, list[tuple[int, str]]]:
    """Remove comments string-aware. Returns (stripped_text, comment_han_lines)."""
    out = []
    comment_han = []
    i, n = 0, len(text)
    line = 1
    in_str = False
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "\n":
            line += 1
            out.append(c)
            i += 1
            continue
        if in_str:
            out.append(c)
            if c == "\\" and nxt:
                out.append(nxt)
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            out.append(c)
            i += 1
            continue
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            seg = text[i : j if j != -1 else n]
            if HAN_RE.search(seg):
                comment_han.append((line, seg.strip()))
            i = j if j != -1 else n
            continue
        if c == "/" and nxt == "*":
            j = text.find("*/", i)
            seg = text[i : (j + 2) if j != -1 else n]
            if HAN_RE.search(seg):
                comment_han.append((line, seg.strip()[:120]))
            i = (j + 2) if j != -1 else n
            continue
        out.append(c)
        i += 1
    return "".join(out), comment_han


def split_statements(stripped: str) -> list[tuple[int, str]]:
    """Split into statements at ; { } boundaries, keeping the first line no."""
    stmts = []
    buf: list[str] = []
    start_line = 1
    line = 1
    for c in stripped:
        if c == "\n":
            line += 1
        buf.append(c)
        if c in ";{}":
            stmt = "".join(buf).strip()
            if stmt:
                stmts.append((start_line, stmt))
            buf = []
            start_line = line
    tail = "".join(buf).strip()
    if tail:
        stmts.append((start_line, tail))
    return stmts


def literals_in(stmt: str) -> list[str]:
    return STRING_LIT_RE.findall(stmt)


NON_TEXT_RE = re.compile(
    r"://|\.(png|svg|jpe?g|json|css|qss|csv|gpkg|geojson|shp|tiff?|qml|html?|md|txt|qml|qm|ts|py|xml|yaml|yml)\s*$",
    re.I,
)


def looks_like_text(lit: str) -> bool:
    """Bare-literal heuristic: ids, paths and resource keys are not UI copy."""
    inner = lit[1:-1]
    if not inner or not inner.strip():
        return False
    if NON_TEXT_RE.search(inner):
        return False
    if not re.search(r"[A-Za-z0-9\u4e00-\u9fff]", inner):
        return False
    if " " not in inner and not HAN_RE.search(inner):
        return False
    return True


def is_concat(stmt: str, api_pos: int) -> bool:
    """After the API call opening, does the argument involve concatenation?"""
    args = stmt[api_pos:]
    return bool(re.search(r'"\s*(?:<<|\+)|\+\s*"|QString::number\s*\(|QString::arg\s*\(', args))


def classify_file(path: Path) -> dict:
    raw = path.read_text(encoding="utf-8", errors="replace")
    stripped, comment_han = strip_comments(raw)
    counts = {
        "tr_wrapped_en": 0,
        "tr_zh_source": 0,
        "hard_zh": 0,
        "hard_en": 0,
        "fmt_concat": 0,
        "nonui_zh": 0,
        "log_zh": 0,
        "comment_zh": len(comment_han),
    }
    findings: list[dict] = []

    for ln, stmt in split_statements(stripped):
        if LOG_RE.search(stmt):
            lits = literals_in(stmt)
            zh = [x for x in lits if HAN_RE.search(x)]
            if zh:
                counts["log_zh"] += len(zh)
                for x in zh:
                    findings.append({"line": ln, "class": "log_zh", "text": x[:90]})
            continue

        uv = USER_VISIBLE_RE.search(stmt)
        has_tr = TR_WRAPPER_RE.search(stmt)
        lits = literals_in(stmt)

        if uv:
            if has_tr:
                for x in lits:
                    if HAN_RE.search(x):
                        counts["tr_zh_source"] += 1
                        findings.append({"line": ln, "class": "tr_zh_source", "text": x[:90]})
                    else:
                        counts["tr_wrapped_en"] += 1
                continue
            concat = is_concat(stmt, uv.end())
            for x in lits:
                if HAN_RE.search(x):
                    counts["hard_zh"] += 1
                    findings.append({"line": ln, "class": "hard_zh", "text": x[:90]})
                elif concat:
                    counts["fmt_concat"] += 1
                    findings.append({"line": ln, "class": "fmt_concat", "text": x[:90]})
                elif looks_like_text(x):
                    counts["hard_en"] += 1
                    findings.append({"line": ln, "class": "hard_en", "text": x[:90]})
            continue

        # Not a user-visible sink: Chinese literals here are non-UI faces.
        if not has_tr:
            zh = [x for x in lits if HAN_RE.search(x)]
            if zh:
                counts["nonui_zh"] += len(zh)
                for x in zh:
                    findings.append({"line": ln, "class": "nonui_zh", "text": x[:90]})
        else:
            for x in lits:
                if HAN_RE.search(x):
                    counts["tr_zh_source"] += 1
                    findings.append({"line": ln, "class": "tr_zh_source", "text": x[:90]})
                else:
                    counts["tr_wrapped_en"] += 1

    return {"file": str(path), "counts": counts, "findings": findings}


def selftest() -> int:
    import tempfile

    ok = True
    with tempfile.TemporaryDirectory() as td:
        # Constructive positive: bare Chinese into setText must be hard_zh.
        p1 = Path(td) / "pos.cpp"
        p1.write_text(
            'void f() {\n'
            '  label->setText( "批量处理" );\n'
            '  box.setWindowTitle( "导出对话框" );\n'
            '}\n',
            encoding="utf-8",
        )
        r1 = classify_file(p1)
        if not (r1["counts"]["hard_zh"] == 2):
            print(f"SELFTEST FAIL pos: {r1['counts']}")
            ok = False
        # Constructive negative: tr()-wrapped + comments must not be hard_*.
        p2 = Path(td) / "neg.cpp"
        p2.write_text(
            '// 流程图注释中文\n'
            'void f() {\n'
            '  label->setText( tr( "Batch processing" ) );  // 尾注释\n'
            '}\n',
            encoding="utf-8",
        )
        r2 = classify_file(p2)
        if not (
            r2["counts"]["hard_zh"] == 0
            and r2["counts"]["hard_en"] == 0
            and r2["counts"]["tr_wrapped_en"] == 1
            and r2["counts"]["comment_zh"] == 2
        ):
            print(f"SELFTEST FAIL neg: {r2['counts']}")
            ok = False
        # Concat: bare literal + number into setText → fmt_concat.
        p3 = Path(td) / "cat.cpp"
        p3.write_text(
            'void f(int n) {\n'
            '  label->setText( "共 " + QString::number( n ) + " 个" );\n'
            '}\n',
            encoding="utf-8",
        )
        r3 = classify_file(p3)
        if r3["counts"]["fmt_concat"] + r3["counts"]["hard_zh"] < 2:
            print(f"SELFTEST FAIL concat: {r3['counts']}")
            ok = False
        # Log exemption.
        p4 = Path(td) / "log.cpp"
        p4.write_text(
            'void g() {\n'
            '  qDebug() << "加载失败" << path;\n'
            '}\n',
            encoding="utf-8",
        )
        r4 = classify_file(p4)
        if r4["counts"]["log_zh"] != 1 or r4["counts"]["hard_zh"] != 0:
            print(f"SELFTEST FAIL log: {r4['counts']}")
            ok = False
    print("SELFTEST " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", help="file list (one path per line); default: scan list from stdin")
    ap.add_argument("--json", help="write full findings JSON here")
    ap.add_argument("--tsv", help="write per-file counts TSV here")
    ap.add_argument("--gate", help="baseline JSON: fail if hard_zh/hard_en/fmt_concat exceed it")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    files = [l.strip() for l in open(args.files, encoding="utf-8") if l.strip()] if args.files else [
        l.strip() for l in sys.stdin if l.strip()
    ]
    results = [classify_file(Path(f)) for f in files]

    totals = {}
    for r in results:
        for k, v in r["counts"].items():
            totals[k] = totals.get(k, 0) + v

    if args.tsv:
        with open(args.tsv, "w", encoding="utf-8") as out:
            out.write("file\ttr_wrapped_en\ttr_zh_source\thard_zh\thard_en\tfmt_concat\tnonui_zh\tlog_zh\tcomment_zh\n")
            for r in results:
                c = r["counts"]
                out.write(
                    f"{r['file']}\t{c['tr_wrapped_en']}\t{c['tr_zh_source']}\t{c['hard_zh']}\t{c['hard_en']}\t"
                    f"{c['fmt_concat']}\t{c['nonui_zh']}\t{c['log_zh']}\t{c['comment_zh']}\n"
                )
    if args.json:
        Path(args.json).write_text(
            json.dumps({"totals": totals, "files": results}, ensure_ascii=False, indent=1),
            encoding="utf-8",
        )
    if args.gate:
        base = json.loads(Path(args.gate).read_text(encoding="utf-8"))["totals"]
        for k in ("hard_zh", "hard_en", "fmt_concat"):
            if totals.get(k, 0) > base.get(k, 0):
                print(f"GATE FAIL: {k} {totals.get(k, 0)} > baseline {base.get(k, 0)}")
                return 1
        print("GATE PASS", {k: totals.get(k, 0) for k in ("hard_zh", "hard_en", "fmt_concat")})
    print(
        f"TOTALS tr_wrapped_en={totals.get('tr_wrapped_en', 0)} tr_zh_source={totals.get('tr_zh_source', 0)} hard_zh={totals.get('hard_zh', 0)} "
        f"hard_en={totals.get('hard_en', 0)} fmt_concat={totals.get('fmt_concat', 0)} "
        f"nonui_zh={totals.get('nonui_zh', 0)} log_zh={totals.get('log_zh', 0)} "
        f"comment_zh={totals.get('comment_zh', 0)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
