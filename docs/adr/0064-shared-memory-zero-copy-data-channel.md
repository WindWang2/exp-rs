# ADR 0064: Shared Memory Zero-Copy Data Channel

## Status
Accepted

## Context
The Python plugin isolation spec (`2026-07-28-python-plugin-isolation-spec.md`)
planned a `shared_memory_segment.h/.cpp` RAII manager with a header
(UUID/dimensions/dtype) so the Python side can mount the payload via
`numpy.frombuffer` without any copy. This file did not exist in the codebase;
`worker_daemon.py` carried dead `import mmap` / `import numpy` lines with no
implementation behind them.

Investigation confirmed that **no pixel data currently crosses the process
boundary**: C++ and Python each read/write the same raster file via GDAL, and
the IPC channel carries only KB-scale JSON paths/scalars. So this is a
**preventive capability build**, not an optimization of an existing bottleneck.
The goal is to land the infrastructure the spec calls for, with an end-to-end
proof that C++ can share a raster buffer with Python without copying, so future
work can wire it into the execution path.

## Decision
1. **`SharedMemorySegment`** (`src/python/isolated/shared_memory_segment.h/.cpp`):
   an RAII wrapper around `QSharedMemory` with a fixed 32-byte header
   (`uuid[16] + width/height/bands/dtype int32`) prepended to the payload. The
   header lets the Python side skip it with a hardcoded offset and mount the
   payload as a numpy array. `static_assert(Header == 32)` enforces the layout.

2. **PosixRealtime, not SystemV**: Qt6's `QSharedMemory::setKey()` defaults to
   `legacyDefaultTypeForOs()`, which on Linux is **SystemV IPC** (`ftok`-based,
   backed by `/tmp/` files). Python's `multiprocessing.shared_memory` only
   supports **POSIX `shm_open`** (`/dev/shm/`). These are incompatible. The
   segment explicitly uses `setNativeKey(key, QNativeIpcKey::Type::PosixRealtime)`
   so both sides speak POSIX shm. The native key (what `shm_open` uses) is
   exposed via `nativeKey()` and passed to Python.

3. **`shm.read` RPC method** in `worker_daemon.py`: Python attaches to the
   segment via `multiprocessing.shared_memory.SharedMemory(name=key)`, skips the
   32-byte header, mounts the payload with `numpy.ndarray(..., buffer=shm.buf,
   offset=32)`, and returns a checksum. No copy of the pixel data is made on
   either side.

4. **End-to-end tests**: two Catch2 cases (float32 and uint8 dtypes) create a
   segment in C++, write known data, ask the Python worker to read it and
   return `arr.sum()`, and assert the checksum matches an independent C++
   computation. Both pass.

## Consequences
- The zero-copy data channel exists and is proven: a future execution-path
  change can pass a `__shm_key__` in `processing.execute_algorithm` params
  instead of a file path, and Python plugins can operate on the shared buffer
  directly with numpy.
- `QSharedMemory::setKey()` (SystemV default) must not be used for cross-
  language shm; always use `setNativeKey(key, PosixRealtime)`. This is the
  single most important implementation detail - using the default produces
  `/tmp/`-based SystemV keys that Python cannot attach to.
- Segment lifetime: C++ detaches on `SharedMemorySegment` destruction;
  `QSharedMemory` on POSIX does not `shm_unlink` automatically, so `/dev/shm/`
  entries persist until explicitly unlinked or the system reboots. For
  short-lived segments (per-algorithm) this is acceptable; a future cleanup
  pass or `shm_unlink` after the Python side detaches would close the leak.
- Windows is out of scope (the project targets Linux); `PosixRealtime` is not
  available there.

## Future
- Wire `__shm_key__` into the execution closure (`app_interface_bridge.cpp`) so
  Python algorithms receive raster blocks via shm instead of file paths.
- Tile-by-tile streaming for rasters too large for a single segment.
- Bidirectional shm (Python writes results back).
- Explicit `shm_unlink` cleanup (either C++ side after Python confirms detach,
  or a `shm.unlink` RPC method).
