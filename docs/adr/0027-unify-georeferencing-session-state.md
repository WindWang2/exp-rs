# 0027 Unify Georeferencing Session State Architecture

We decided to absorb `RsGeorefSessionState` into `RsGeoreferencingSession`, unifying all GCP pairs, transform fitting, residual calculations, dirty flag tracking, and `QSettings` workflow persistence inside a single deep session module.

### Context & Decision
Previously, georeferencing state was split across `RsGeoreferencingSession` (GCP pairs, transform fitting, warp snapshots) and a sidecar class `RsGeorefSessionState` (`src/app/georeferencer/rs_georef_session_state.h`/`.cpp` holding `mDirty` and `QSettings` persistence). UI dialogs had to sync both objects manually, leading to missed dirty-flag updates and redundant path tracking.

1. **Single Deep Authority**: Deepen `RsGeoreferencingSession` to absorb `isDirty()`, `markDirty()`, `clearDirty()`, `struct WorkflowSnapshot`, `saveWorkflow()`, `restoreWorkflow()`, `saveWindow()`, and `restoreWindow()`.
2. **Automatic Dirty State Mutations**: All GCP mutations (`addGcp`, `removeGcpAt`, `setGcpEnabled`, `clearGcps`) and parameter setters (`setSourceRasterPath`, `setTransformMethod`, `setDemPath`) automatically set `mDirty = true`.
3. **Sidecar Deletion**: Deleted `src/app/georeferencer/rs_georef_session_state.h` and `src/app/georeferencer/rs_georef_session_state.cpp`.
