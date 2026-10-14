"""R12-G G09 GREEN oracle. Self-contained: builds its own fixtures.

    python g09_green.py control   # a correct bundle must PASS
    python g09_green.py after     # every escape must FAIL (the fix)

The link fixture is created here rather than assumed: a Windows directory
junction (reparse tag 0xA0000003) where one is possible, a POSIX symlink
otherwise. Both take the same code path in the verifier -- os.path.islink()
is False for a junction, which is precisely why the escape used to pass.
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
VERIFIER = os.path.normpath(os.path.join(HERE, "..", "..", "scripts",
                                         "verify_bundle_manifest.py"))

BASE = os.path.join(tempfile.gettempdir(), "r12g-g09")
ROOT = os.path.join(BASE, "bundle")
OUTSIDE = os.path.join(BASE, "outsidedir")
LINK = os.path.join(ROOT, "jlink")

EXPECT = {"control": 0, "1a": 1, "1b": 1, "5": 1}
results = []


def sha(p):
    with open(p, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def entry(rel, root=ROOT):
    target = os.path.join(root, rel.replace("/", os.sep))
    return {"path": rel, "bytes": os.path.getsize(target), "sha256": sha(target)}


def write_manifest(files):
    m = {"schema": "sicnu.offline_bundle/2", "version": "0",
         "size_ceiling_mb": 250, "files": files}
    with open(os.path.join(ROOT, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump(m, fh, indent=1)


def run(label, expect):
    r = subprocess.run([sys.executable, VERIFIER, ROOT],
                       capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    print("--- %s ---" % label)
    print(out)
    print("exit=%d (expected %d)" % (r.returncode, expect))
    ok = r.returncode == expect
    print("=> %s\n" % ("PASS" if ok else "MISMATCH"))
    results.append((label, ok))
    return r.returncode


def drop_link():
    """Remove a leftover link fixture so a control run starts clean."""
    if not os.path.lexists(LINK):
        return
    if os.path.isdir(LINK) and not os.path.islink(LINK):
        os.rmdir(LINK)
    else:
        os.remove(LINK)


def build_clean():
    os.makedirs(os.path.join(ROOT, "bin"), exist_ok=True)
    with open(os.path.join(ROOT, "hello.txt"), "wb") as fh:
        fh.write(b"hello\n")
    with open(os.path.join(ROOT, "bin", "tool.dat"), "wb") as fh:
        fh.write(b"tool\n")
    os.makedirs(OUTSIDE, exist_ok=True)
    with open(os.path.join(OUTSIDE, "secret.txt"), "wb") as fh:
        fh.write(b"OUTSIDE-SECRET\n")
    drop_link()
    write_manifest([entry("hello.txt"), entry("bin/tool.dat")])


def make_link():
    """Create the escape fixture. Returns a description of what was made."""
    drop_link()
    if os.name == "nt":
        # A junction needs no privilege, unlike a symlink, and is the case
        # os.path.islink() misses.
        cmd = ["powershell", "-NoProfile", "-Command",
               "New-Item -ItemType Junction -Path '%s' -Target '%s'"
               % (LINK, OUTSIDE)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode == 0 and os.path.isdir(LINK):
            return "junction (Windows)"
        print("junction creation failed: %s" % (r.stderr or r.stdout).strip())
    try:
        os.symlink(OUTSIDE, LINK, target_is_directory=True)
        return "symlink (POSIX)"
    except OSError as exc:
        print("could not create any link fixture: %s" % exc)
        return None


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "control"
    if mode == "control":
        build_clean()
        run("CONTROL: a correct bundle must PASS", EXPECT["control"])
    elif mode == "after":
        build_clean()
        kind = make_link()
        if kind is None:
            print("no link fixture -> cannot prove G09-1; NOT_RUN")
            return 3
        st = os.lstat(LINK)
        print("link fixture: %s" % kind)
        print("  islink=%s isdir=%s reparse=%s"
              % (os.path.islink(LINK), os.path.isdir(LINK),
                 bool(getattr(st, "st_file_attributes", 0) & 0x400)))
        # 1a: the link exists but the manifest does not list its content.
        run("G09-1a: link present, files[] omits its content", EXPECT["1a"])
        # 1b: the attack. Register the OUT-OF-BUNDLE file as bundle content,
        # with the size and digest taken from the real outside file. Before
        # the fix this PASSED (exit 0) and the out-of-bundle bytes were
        # hashed as bundle content.
        secret = os.path.join(OUTSIDE, "secret.txt")
        write_manifest([entry("hello.txt"), entry("bin/tool.dat"),
                        {"path": "jlink/secret.txt",
                         "bytes": os.path.getsize(secret),
                         "sha256": sha(secret)}])
        run("G09-1b: files[] registers out-of-bundle content", EXPECT["1b"])
        # G09-5: ./manifest.json must be rejected by name, not degrade into a
        # generic size mismatch.
        write_manifest([entry("hello.txt"), entry("bin/tool.dat"),
                        {"path": "./manifest.json", "bytes": 1,
                         "sha256": "x"}])
        run("G09-5: ./manifest.json must be rejected by name", EXPECT["5"])
    else:
        print("unknown mode %r (control|after)" % mode)
        return 2

    bad = [label for label, ok in results if not ok]
    if bad:
        print("ORACLE FAILED: %s" % ", ".join(bad))
        return 1
    print("ORACLE PASS: %d/%d" % (len(results), len(results)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
