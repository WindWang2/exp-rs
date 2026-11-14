# Vendored managed Python

`runtime.lock.json` pins CPython **3.12.16**, Python Standalone Builds release
**20261110**, and each platform archive's upstream SHA-256 digest. The portable
`install_only_stripped` distribution includes the interpreter, standard library,
native extension dependencies, pip, venv/ensurepip and upstream licenses.
The complete archive contents are retained. Large fetched artifacts stay in the
build cache rather than Git.

Upstream sources: [release and checksums](https://github.com/astral-sh/python-build-standalone/releases/tag/20261110),
[distribution layout](https://docs.astral.sh/python-build-standalone/distributions/).
This is a standalone external process runtime; the GUI does not embed Python.

Prepare a native install-prefix tree before configuring a release package:

```sh
python scripts/prepare_python_runtime.py --destination build-release/python-runtime
cmake --preset release-package
```

The preparation downloads and verifies the pinned runtime, then uses that
interpreter to download binary wheels matching the bundled, hash-locked
`geospatial-v1` profile. The wheelhouse enables first-use environment creation
without a package index or system Python on the end-user machine. The default
cache is `build-logs/python-vendor-cache`; `--cache-dir` selects another cache,
and `--offline` refuses all downloads.

`SICNU_BUNDLE_PYTHON=ON` activates the `PythonRuntime` install component.
`SICNU_PYTHON_RUNTIME_STAGE` selects an already prepared tree; it defaults to
`<build>/python-runtime`. Configure and install both verify its complete file
inventory, archive provenance and dependency lock, and fail if it is missing,
modified or prepared for another platform. Configuration never downloads code.

Installed paths:

- `libexec/sicnu-geo-rs/python/`: complete portable runtime and licenses.
- `share/sicnu-geo-rs/python-wheels/geospatial-v1/`: locked wheels and requirements.
- `share/sicnu-geo-rs/python-runtime.json`: version, upstream URL/digest and file receipt.

Verify the installed runtime after moving the whole install prefix, with the
original staging tree unavailable:

```sh
python scripts/prepare_python_runtime.py --smoke /path/to/relocated-install
```

The smoke test uses only the installed interpreter and wheels: it imports
native stdlib modules, creates a fresh user environment, installs with
`--no-index --require-hashes`, imports NumPy/Rasterio and writes/reads a real
NDVI change-mask GeoTIFF. No prebuilt virtual environment is shipped, because
venvs contain absolute interpreter paths and are recreated per installation.
