"""Exercise native GDAL's EPSG lookup from a Unicode Windows install."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def check_projection(cli: Path, database: Path, cwd: str, env: dict[str, str]) -> None:
    result = subprocess.run(
        [str(cli), "env-doctor", "--json"],
        cwd=cwd,
        env=env,
        stdin=subprocess.DEVNULL,
        capture_output=True,
        creationflags=subprocess.CREATE_NO_WINDOW,
        timeout=90,
    )
    # The doctor uses exit 2 even for unrelated warnings, such as an absent
    # optional SDK component. Require its actual projection checks to pass.
    mode = "with PROJ_DATA" if "PROJ_DATA" in env else "without PROJ_DATA"
    if result.returncode not in (0, 2):
        raise RuntimeError(f"Installed env-doctor {mode} exited {result.returncode}")
    report = json.loads(result.stdout)["data"]
    if report["schema"] != "exp.env.report.v1":
        raise RuntimeError(f"Unexpected environment report schema {report['schema']!r}")
    checks = {entry["check"]: entry for entry in report["checks"]}
    crs = checks["proj.crs.resolve"]
    if crs["severity"] != "ok":
        raise RuntimeError(f"Native EPSG lookup {mode} failed: {crs['message']}")
    finding = checks["proj.db"]
    resolved = finding.get("detail", {}).get("resolved")
    if finding["severity"] != "ok" or not resolved or Path(resolved).resolve() != database:
        raise RuntimeError(f"Expected bundled {database!s} {mode}, reported {resolved!r}")
    print(f"Installed native EPSG lookup succeeded {mode} using {database}", flush=True)


def main(prefix: Path) -> None:
    prefix = prefix.resolve(strict=True)
    if str(prefix).isascii() or " " not in str(prefix):
        raise RuntimeError("The Windows install directory must contain Unicode and a space")
    cli = (prefix / "bin/sicnu_geo_rs_cli.exe").resolve(strict=True)
    database = (prefix / "share/proj/proj.db").resolve(strict=True)
    if not database.is_file():
        raise RuntimeError(f"Missing bundled projection database: {database}")
    env = os.environ.copy()
    for name in ("PROJ_DATA", "PROJ_LIB", "PROJ_AUX_DB"):
        env.pop(name, None)
    env.update(QT_QPA_PLATFORM="offscreen", PROJ_NETWORK="OFF", SICNU_OFFLINE="1")
    with tempfile.TemporaryDirectory(prefix="sicnu-proj-smoke-") as cwd:
        env.update(APPDATA=cwd, LOCALAPPDATA=cwd, USERPROFILE=cwd, HOME=cwd,
                   QGIS_CUSTOM_CONFIG_PATH=cwd, QGIS_AUTH_DB_DIR_PATH=cwd,
                   SICNU_LLM_KEYCHAIN="0")
        check_projection(cli, database, cwd, env)
        check_projection(cli, database, cwd, {**env, "PROJ_DATA": str(database.parent)})


if __name__ == "__main__":
    main(Path(sys.argv[1]))
