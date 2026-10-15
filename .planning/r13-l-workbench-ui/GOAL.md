# Track R13-L: Goals & Work Packages

## WP-01: Thread Reaper Lifecycle & Join Safety (#1493, DEF-PR1501-01)
- Eliminate all unmanaged `std::thread(...).detach()` calls in dock worker retirement.
- Implement structured reaper thread tracking (`m_reaperThreads` / `m_retiredThreads`) protected by mutex.
- In dock destructors, signal cancellation and join all active and retired threads before releasing dock resources.
- Protect against concurrent worker execution when a session is restarted quickly.

## WP-02: Pipeline Definition Durability & Symlink Preservation (#1487)
- Preserve POSIX file permissions during atomic pipeline replacement (avoid default 0600 clobbering).
- Resolve symlink targets before writing so the target file is updated rather than the symlink being unlinked.
- Implement Windows atomic replacement (`ReplaceFileW` with fallback) instead of non-atomic `.bak` swap.
- Ensure file and directory fsync/commit are called.

## WP-03: Experiment Studio Display Bounding & Lifecycle Safety (#1492)
- Bound matrix table rendering to `kMaxMatrixDisplayRows = 500` to prevent memory exhaustion on large studies.
- Ensure study worker thread is cleanly retired and joined without detached reaper leaks.

## WP-04: Honest Cut Feedback in Vector Editing (#1453)
- Verify `deleteSelectedFeatures()` return value in `MainWindowVector::cutSelectedFeatures()`.
- Roll back the edit command if deletion fails and report truthful status.

## Verification Gate
- Unit tests compile and pass under `-j2` build and `-j1` test constraints under the global physical lock.
- 100% green tests twice consecutively (GREEN x 2).
- Zero detached thread leaks in unit tests.
- Independent code review pass.
