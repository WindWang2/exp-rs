# Specification: Decoupling PythonWorkerProcessPool Lifecycle Management

## Problem Statement

Currently, worker process acquisition and restart policies are tightly coupled to static initializers. A worker node release should automatically purge stale workers and emit pool statistics cleanly without requiring callers to manually inspect node status flags.

## Solution

Deepen `PythonWorkerProcessPool` by adding `poolSize()` and `resetPool(int poolSize)` methods, ensuring worker processes are managed in a deep module pattern (ADR 0045).

## User Stories

1. As a Developer, I want `PythonWorkerProcessPool` to allow dynamic pool size configuration via `resetPool(int poolSize)`.
2. As a System Administrator, I want `poolHealth()` to accurately report pool size, active nodes, and cumulative restarts across resets.
3. As a Unit Test, I want process pool lifecycle operations to be testable without instantiating full Qt application plugin hosts.

## Implementation Decisions

- **Dynamic Pool Resizing**: Add `int poolSize() const` and `bool setPoolSize(int poolSize)` to `PythonWorkerProcessPool`.
- **Clean Health Accounting**: Ensure `poolHealth()` computes total node count dynamically.

## Testing Decisions

- **Seam**: Test process pool management using `test_python_plugin_host.cpp`.

## Out of Scope

- Modifying IPC local socket wire protocols.

## Further Notes

Aligned with ADR 0045 (Worker Pool Health Monitoring) and ADR 0053.
