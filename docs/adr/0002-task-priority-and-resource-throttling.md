# 0002 Task Priority and Resource Throttling Architecture

We decided to add `TaskPriority` (`High`, `Normal`, `Low`) to `AlgorithmTaskInfo` and auto-cap concurrent background task execution to `std::thread::hardware_concurrency() - 1`.

### Context & Decision
To prevent UI lockups during multi-task remote sensing analysis:
1. **Priority Queue Sorting**: `TaskCenter` sorts all `Queued` tasks primarily by `TaskPriority` (High > Normal > Low) and secondarily by submission timestamp.
2. **Auto Thread Capping**: Maximum active running tasks is bounded by `std::thread::hardware_concurrency() - 1` (with a floor of 1 thread), guaranteeing at least 1 core remains unallocated for UI canvas rendering and user interaction.
