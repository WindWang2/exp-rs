#!/usr/bin/env python3
"""Demo external tool: wc-style counter (example plugin payload)."""
import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: wordcount.py <file>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as exc:
        print(f"cannot read {path}: {exc}", file=sys.stderr)
        return 1
    result = {
        "success": True,
        "output": path,
        "bytes": len(data),
        "lines": data.count(b"\n"),
        "words": len(data.split()),
    }
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
