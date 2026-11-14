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

The fixed scientific profile contains NumPy, Rasterio, Pandas, SciPy,
Matplotlib, PyProj and Shapely. Windows additionally includes
`gdal-wheel==3.13.3`, imported as `osgeo`. The exact versions and complete
dependency hashes are in `data/python/geospatial-v1/requirements.lock`.
Matplotlib uses Agg for agent tasks; no Python Qt binding is required.

The Windows GDAL wheel is an unofficial downstream distribution published by
[xiejx5/gdal-wheel](https://github.com/xiejx5/gdal-wheel/tree/7fbd27a4fb04ec966fcbc7576a63e8e039b4be09),
with publisher provenance on [PyPI](https://pypi.org/project/gdal-wheel/3.13.3/#files).
The CPython 3.12 x64 artifact is
`gdal_wheel-3.13.3-cp312-cp312-win_amd64.whl`, SHA-256
`e3b6e4cc90e15e66e3de6c0dfc4b8a78e55987944d74f6a186e4179305361430`.
It supplies GDAL 3.13.3/PROJ 9.8.1 with privately named native DLLs and resource
data, independently of the application's native GDAL and Rasterio's libraries.
Preserve the complete wheel, GDAL's MIT license and all dependency notices in
`osgeo/data/licenses` (including GEOS LGPL-2.1). This binding is restricted to
Windows because the selected release lacks wheels for every other supported
runtime target. Native GDAL/Python array interop and both Rasterio/GDAL import
orders are verified before packaging.

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
