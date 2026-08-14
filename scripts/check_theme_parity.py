#!/usr/bin/env python3
"""
check_theme_parity.py - Verifies selector and token parity between styles.qss and styles-dark.qss.
Exit code 0 on parity, 1 on discrepancies.
"""
import re
import sys
from pathlib import Path

def extract_selectors(qss_text: str) -> set[str]:
    # Strip comments
    clean = re.sub(r'/\*.*?\*/', '', qss_text, flags=re.DOTALL)
    selectors = set()
    for match in re.finditer(r'([^{}]+)\{', clean):
        raw = match.group(1).strip()
        # Split grouped selectors like "QTreeView, QTreeWidget, QListView"
        for sel in raw.split(','):
            norm = ' '.join(sel.strip().split())
            if norm:
                selectors.add(norm)
    return selectors

def check_invalid_values(qss_text: str, filename: str) -> list[str]:
    errors = []
    for idx, line in enumerate(qss_text.splitlines(), start=1):
        if re.search(r'border-bottom-color\s*:\s*none', line, re.IGNORECASE):
            errors.append(f"{filename}:{idx} - Invalid CSS: 'border-bottom-color: none'")
        if re.search(r'^\s*opacity\s*:', line, re.IGNORECASE):
            errors.append(f"{filename}:{idx} - Unsupported Qt CSS property: 'opacity'")
        if filename.endswith("styles-dark.qss") and "#f6f8fa" in line:
            errors.append(f"{filename}:{idx} - Light theme color '#f6f8fa' leaked into dark theme!")
    return errors

def main():
    root = Path(__file__).resolve().parent.parent
    light_file = root / "resources" / "styles.qss"
    dark_file = root / "resources" / "styles-dark.qss"

    if not light_file.exists() or not dark_file.exists():
        print(f"Error: QSS files not found under {root / 'resources'}")
        sys.exit(2)

    light_text = light_file.read_text(encoding="utf-8")
    dark_text = dark_file.read_text(encoding="utf-8")

    light_sel = extract_selectors(light_text)
    dark_sel = extract_selectors(dark_text)

    missing_in_dark = sorted(light_sel - dark_sel)
    missing_in_light = sorted(dark_sel - light_sel)

    syntax_errors = check_invalid_values(light_text, "styles.qss") + check_invalid_values(dark_text, "styles-dark.qss")

    failed = False
    if missing_in_dark:
        print(f"FAILED: {len(missing_in_dark)} selectors in Light but missing in Dark:")
        for s in missing_in_dark:
            print(f"  - {s}")
        failed = True

    if missing_in_light:
        print(f"FAILED: {len(missing_in_light)} selectors in Dark but missing in Light:")
        for s in missing_in_light:
            print(f"  - {s}")
        failed = True

    if syntax_errors:
        print(f"FAILED: {len(syntax_errors)} syntax / color leak issues found:")
        for err in syntax_errors:
            print(f"  - {err}")
        failed = True

    if not failed:
        print(f"SUCCESS: Theme selector parity verified ({len(light_sel)} selectors in sync).")
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
