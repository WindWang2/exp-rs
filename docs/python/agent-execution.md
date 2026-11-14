# Managed Python tasks for product agents

The optional Python backend exposes `python:environment` and
`python:run_script` through the same Agent Tool Catalog, MCP and Task Center
as native algorithms. Existing `get_execution_status` and `cancel_execution`
operate on their `task-<id>` execution identities. Each script has its own
process and temporary directory; the environment is reused across tasks.
This MVP delivers one GeoTIFF and JSON statistics per script. Shell execution
is deferred.

## Host configuration

Set `SICNU_PYTHON_EXECUTION=trusted_local` when launching the host to enable
script execution. This grants scripts the host user's filesystem and network
permissions. Tool manifests explicitly classify both Python tools as
`external_process`. A virtual environment isolates dependencies; it is not a
filesystem or network sandbox. Use this MVP for trusted local projects and
reviewed agent scripts. The runner uses a filtered environment and argv-based
process creation, without a shell.

Release packages ship CPython **3.12.16**, including the standard library,
venv/ensurepip and native runtime files. The exact
[python-build-standalone release](https://github.com/astral-sh/python-build-standalone/releases/tag/20261110)
and platform archive hashes are pinned in `vendor/python/runtime.lock.json`.
The host finds the interpreter relative to its installation prefix; it never
falls back to a machine's PATH. A missing or broken packaged runtime is an
error. Source builds can explicitly set `SICNU_PYTHON_EXECUTABLE` to a Python
3.12 interpreter or a staged bundled interpreter. Agent requests cannot
select an interpreter, package source, package list or environment directory.
`SICNU_PYTHON_ENV_ROOT` optionally sets an absolute host-owned environment
root; the default is `sicnu-geo-rs/python-environments` under Qt's generic
data directory, shared by the application and its native worker. No
environment activation is required. Host configuration is fixed at startup;
restart the host after changing it so warm native workers inherit the update.

The bundled `geospatial-v1` profile pins NumPy, Rasterio and all dependencies
in `data/python/geospatial-v1/requirements.lock`. Preparation creates a venv,
installs binary wheels with pip's `--require-hashes` policy, imports the native
libraries, checks installed versions against the universal lock, and writes a
completion marker atomically. Packaged preparation uses the shipped wheelhouse
with `--no-index --find-links`, so first use needs no network. Only an explicit
development interpreter without a bundled wheelhouse uses the package index.
Runtime Python does not require uv; maintainers regenerate the lock using the
command recorded in `requirements.in`.

Environment directories are keyed by the dependency lock and selected runtime
identity, including its installation path. Moving or upgrading the application
therefore selects a fresh environment instead of reusing a venv tied to an old
base interpreter. A preparation lock
prevents concurrent installation; ready environments are immutable. A changed
interpreter, dependency inventory or lock produces an unready diagnostic rather
than silently repairing an environment that running tasks might be using.

## Building a package

Prepare the runtime explicitly on the packaging host, then configure the
release preset:

```text
python scripts/prepare_python_runtime.py --destination build-release/python-runtime
cmake --preset release-package
cmake --build build-release --parallel 2
cmake --build --preset release-package
```

The preparation step verifies the pinned archive hash, preserves the upstream
runtime/license files, and downloads only binary wheels accepted by the bundled
dependency lock. Downloads can be reused with `--cache-dir`; after populating
that cache, `--offline` refuses any download. Python binaries and wheel payloads
are build artifacts; the source repository vendors their exact recipe and hashes.

`SICNU_BUNDLE_PYTHON` defaults on when `SICNU_ENABLE_PACKAGING` is enabled.
`SICNU_PYTHON_RUNTIME_STAGE` selects the prepared prefix (default
`<build-dir>/python-runtime`). Both configure and install verify the complete
runtime/wheel inventory; missing, modified or stale payloads fail the package.
The `PythonRuntime` install component places the interpreter under
`libexec/sicnu-geo-rs/python` and the offline wheelhouse under
`share/sicnu-geo-rs/python-wheels/geospatial-v1`. The application prepares user
environments after installation; no build-machine venv is distributed.

The Windows and POSIX classroom bundle scripts also install this component
when their build enables `SICNU_BUNDLE_PYTHON`, including the manual binary-copy
path. Existing offline lab configurations can leave the component disabled.
The archive size budget must include the runtime and wheels when enabled.

## Agent calls

First inspect readiness, then explicitly prepare when needed:

```json
{"name":"python:environment","arguments":{"profile_id":"geospatial-v1","action":"probe"}}
```

```json
{"name":"python:environment","arguments":{"profile_id":"geospatial-v1","action":"prepare","timeout_seconds":600}}
```

These are ordinary asynchronous tasks. Poll their `execution_id` with
`get_execution_status`. Probe returns `ready`, diagnostics, the interpreter,
actual package versions and the environment/lock SHA-256 values. Script
submission never installs dependencies implicitly.

Agents can pass generated code directly in `script`, or select an existing
workspace file with `script_path`. Provide exactly one source. For the
[NDVI change example](examples/ndvi_change_mask.py), submit the file and
absolute data paths:

```json
{
  "name":"python:run_script",
  "arguments":{
    "profile_id":"geospatial-v1",
    "script_path":"/workspace/ndvi_change_mask.py",
    "inputs":["/workspace/before.tif","/workspace/after.tif"],
    "output":"/workspace/change_mask.tif",
    "parameters":{"threshold":0.2},
    "timeout_seconds":300
  }
}
```

Use native absolute paths on Windows. When `SICNU_MCP_WORKSPACE` is configured,
declared script, inputs and output must resolve inside that workspace. The
output must be a new `.tif`/`.tiff` path. Inputs are an ordered list of existing
files; registered inputs retain their asset identities and revisions in the
output's derivation record.

The script receives four globals:

| Name | Meaning |
|---|---|
| `inputs` | Tuple of resolved input paths |
| `output` | Private temporary GeoTIFF path to write |
| `parameters` | JSON object supplied by the agent |
| `result` | JSON object for small statistics |

An inline call has the same shape with `"script":"import numpy as np\n..."`
in place of `script_path`; no file-writing tool is required. File inputs and
destinations still undergo the workspace checks.

The host snapshots the single UTF-8 script before executing it with isolated
Python flags. Scripts may use the managed packages and standard library. The
script is limited to 128 KiB; parameters and returned statistics are each
limited to 64 KiB. Statistics must be JSON-compatible and contain no NaN or
infinity. Standard output/error are bounded and available in task logs/results.

The NDVI example requires aligned single-band inputs. It processes windows,
preserves grid/CRS and writes Byte values `0` (unchanged), `1` (changed), and
`255` (invalid in either input). It reports changed/valid pixel counts and the
changed fraction. Arbitrary agent scripts are not advertised as safe for large
rasters.

## Cancellation and delivery

```json
{"name":"cancel_execution","arguments":{"execution_id":"task-123"}}
```

Poll until terminal after requesting cancellation. The process executor kills
the process tree on cancellation/timeout. Failed or cancelled scripts discard
their private temporary outputs and do not enter the asset publication step.
The normal Task Center resource gate also applies to preparation and scripts.
These Python tasks are not automatically replayed after a worker crash or a
lost completion reply; any retry is an explicit new request.

Scripts have a fixed 1 GiB native memory budget. On Windows this is the Job
Object's total committed memory; on POSIX it is each process's virtual address
space limit, inherited by descendants. These are different OS measurements.
An OS refusal to apply the budget fails closed; in particular Darwin can
reject lowering the forked host's existing address space before exec.

After successful execution the host validates the JSON statistics and opens
the generated GeoTIFF, checking physical raster reads. The delivery path
supports the GeoTIFF plus PAM/world-file metadata. External
pixel masks, overviews and other unsupported GDAL file dependencies are
rejected before publication so their semantics cannot be lost during cleanup.
OutputCommitter then atomically publishes to the declared destination and
registers the Data Asset on its owning thread. The agent result contains the
committed `output`, `assetId`, statistics, `script_sha256`,
`environment_sha256`, `lock_sha256`, `python_version` and `profile_id`.
`get_lineage` can consume that asset ID. The derivation record keeps input
asset/revision edges and these execution identities in
`parameters.python_execution`. A commit failure is reported as failure, not
as a delivered artifact. JSON statistics remain task results in this MVP.

## Validation

`test_python_agent_mvp` crosses the real MCP protocol dispatch, Task Center,
Python process and DataManager seam. Set `SICNU_TEST_PYTHON_ENV_ROOT` to a
dedicated absolute test environment root to enable the real dependency cases;
the test explicitly skips those cases when that setting is absent, instead of
installing dependencies unexpectedly. It verifies a fixed 4x3 NDVI oracle,
actual GeoTIFF pixels/georeferencing and asset lineage: at threshold `0.2`,
the fixture has 9 valid pixels and 4 changed pixels. It also covers running
cancellation, timeout, script exceptions, missing/invalid output, unsupported
external masks, existing destination preservation, execution opt-in,
unprepared environments and refusal to automatically replay an uncertain task.
The same root can be reused without moving the venv.
`test_python_process_limits` exercises actual stdlib Python allocation and
cancellation without NumPy/Rasterio.

To exercise the optional outer native-worker route, build `sicnu_worker`, set
`SICNU_WORKER_EXECUTION=require` and `SICNU_WORKER_PROGRAM` to its absolute
path, then run the NDVI and cancellation cases in separate test processes.
Each process starts workers with its fixture's workspace policy.
`test_worker_host` also verifies cold/prewarmed pool handoff across three
caller threads and shutdown from a different thread.

Pi users can add `python` to `EXP_RS_TOOL_CATEGORIES` when selecting tools.
