# Track R13-L: Decisions & Rationales

## D-01: Reaper Thread Tracking vs Immediate Blocking Join
- **Context**: In GUI docks, calling `join()` directly on the GUI main thread inside `startWorker()` blocks UI responsiveness if the previous worker takes a few milliseconds to wind down. However, calling `std::thread(...).detach()` leaves orphaned threads that may race or dereference freed dock memory on exit (`DEF-PR1501-01`).
- **Decision**: Introduce a `std::vector<std::thread> m_reaperThreads;` guarded by `std::mutex m_reaperMutex;`. When retiring a worker in `startWorker()`, launch a background joiner that joins the old worker and push the joiner into `m_reaperThreads`. Before pushing, prune already-completed joiners. In the dock destructor, signal cancellation and join all threads in `m_reaperThreads` as well as the active worker thread.
- **Tradeoff**: Non-blocking on the GUI thread during normal operation; guaranteed deterministic cleanup upon dock destruction.

## D-02: Pipeline Editor Symlink and Permission Preservation
- **Context**: Overwriting an existing file via `rename()` from a `QTemporaryFile` replaces the inode, which resets permissions to `0600` and breaks symlinks by replacing the symlink itself with the regular temporary file.
- **Decision**:
  1. Use `QFileInfo::canonicalFilePath()` / `readlink` to resolve symlink targets before staging the temporary file.
  2. Query `QFile::permissions(targetPath)` before writing and restore the permissions on the temporary file before renaming.
  3. On Windows: use `ReplaceFileW` with `MOVEFILE_WRITE_THROUGH` to atomically replace without metadata loss.
- **Tradeoff**: Fully preserves user intent and filesystem semantics across POSIX and Windows.

## D-03: Matrix Display Virtualization / Bounding
- **Context**: Visualizing multi-dimensional parameter grids with 10,000+ points synchronously instantiates tens of thousands of `QTableWidgetItem` objects on the heap, stalling the GUI and consuming hundreds of MBs.
- **Decision**: Cap the displayed matrix preview at `kMaxMatrixDisplayRows = 500`, displaying an informative label when truncated. The underlying study specification remains completely unaffected.
