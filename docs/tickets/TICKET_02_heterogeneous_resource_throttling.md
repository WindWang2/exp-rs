# Ticket TICKET-02: Heterogeneous Resource Throttling & Priority Scheduling

- **Type**: `grilling`
- **Status**: Closed
- **Parent Map**: [MAP_algorithm_engine_modularization.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_algorithm_engine_modularization.md)

## Question

How should `TaskCenter`'s `ResourceThrottler` manage concurrency and priority scheduling across heterogeneous provider execution backends (in-process C++ threads, Python worker pools, CLI sub-processes, and `QgsTask`)?

## Resolution

Adopt **Option A**: Implement `ProviderResourceProfile` for provider-aware scheduling in `TaskCenter`.

### Key Architectural Details
- **Resource Profiles**:
  1. `InProcessThread`: Bounded to `std::thread::hardware_concurrency() - 1` worker threads.
  2. `PythonWorkerProcess`: Delegated to `PythonWorkerProcessPool` pre-warmed worker daemon slots.
  3. `ExternalCliSubprocess`: Throttled to prevent process table and I/O saturation.
  4. `QgsTaskThread`: Delegated to QGIS native task manager thread pool.
- **Priority Queueing**: All tasks share unified `High`, `Normal`, and `Low` priority scheduling queues.
