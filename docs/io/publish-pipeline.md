# The Unified Publish Pipeline (7.0)

Every write path in `sicnu_geospatial` — raster writer, vector writer,
conversion kernels (translate / warp / COG / overview / vector convert) —
publishes through one pipeline in `util/atomic_fs`:

```
stage (target directory, same volume) → write → fsync → validate
      → backup existing members (sidecars AND main file) → publish
      → on failure: remove published, restore backups, throw
```

## Invariants (all enforced by atomic_fs, verified by test_io_atomic_failures)

1. **Same-volume staging**: staging files are created next to the target, so
   publication is a same-volume rename (atomic). Cross-device failure
   (EXDEV) falls back to copy-into-target-directory + fsync + same-directory
   rename (#807).
2. **Main file last**: a dataset group (shapefile family, GPKG, sidecars)
   publishes sidecars first and the main file last — the main file's
   presence is the completeness marker.
3. **Backup set covers the main file**: replaced main files and sidecars
   move aside to `<name>.bak` before publication; a mid-publish failure
   restores the previous good group and re-throws with the failed member
   name (#791). A failed restore must never drop a backup.
4. **Validation before publish**: raster/vector writers re-open the staged
   dataset before publishing; a validation failure discards staging and
   leaves the target untouched.
5. **Cancellation**: `cancel()` on any writer removes the whole staged set —
   target and previous group stay untouched.
6. **Quiet hygiene**: helper files (`*.publish-cross-device`, backups) are
   consumed or removed on every path; nothing half-published remains.

## Windows / POSIX parity

- POSIX: `rename(2)` (atomic replace) with the EXDEV copy fallback.
- Windows: `ReplaceFileW` when the target exists (transactional), else
  `MoveFileExW(REPLACE_EXISTING | COPY_ALLOWED | WRITE_THROUGH)`.
  `COPY_ALLOWED` sacrifices atomicity only in the cross-volume case, which
  the staging convention avoids in the first place.
- All path handling stays UTF-8 (never the active code page).

## Where each entry point lands

| Entry point | Pipeline |
|---|---|
| `RasterWriter::finalize` | `publishStagedGroup` |
| `VectorWriter::finalize` | `publishStagedGroup` |
| `translateRaster` / `warpRaster` / `makeCog` | stage → validate → `publishStagedGroup` |
| `vectorConvert` | through `VectorWriter` (same as above) |
| plain text/JSON sidecars | `writeFileAtomic` |
| overview building | sanctioned in-place (derived data only) |
