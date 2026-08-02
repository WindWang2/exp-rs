# ADR 0034: Consolidate Georeferencing Session Warp Execution

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`RsGeoreferencingSession` previously delegated warp task submission and cancellation through an abstract interface `RsGeorefWarpExecutor`, with `RsGeorefTaskCenterExecutor` being its only production adapter wrapper over `TaskCenter::instance()`.

## Decision

1. **Direct TaskCenter Integration**: `RsGeoreferencingSession::submitWarpTask` and `cancelWarpTask` delegate directly to `sicnu::TaskCenter::instance()` by default.
2. **Delete Shallow Wrappers**: Delete `rs_georef_task_center_executor.h` and the `RsGeorefWarpExecutor` abstract class.
3. **Test Injection Callback**: Provide `setCustomWarpExecutor` on `RsGeoreferencingSession` to allow headless Catch2 unit tests to inject custom mock execution functions without interface class hierarchies.

## Consequences

- **Locality**: Warp task creation and `TaskCenter` status observation concentrate inside `RsGeoreferencingSession`.
- **Simplification**: One shallow adapter header and interface class removed from the project.
- **Testability**: Unit tests mock warp submission via a simple `std::function` callback.
