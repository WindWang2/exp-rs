# Architectural Decisions: R13-J Security and Concurrency Hardening

## Decision 1: Hostname Resolution in Geospatial Subsystem
- **Context**: `sicnu_geospatial` is a Qt-free C++20 library.
- **Decision**: Use standard POSIX `getaddrinfo` with `sockaddr_in` and `sockaddr_in6`.
- **Rationale**: Avoid adding Qt dependencies (such as `QHostInfo`) into `sicnu_geospatial`, keeping the library lightweight, portable, and adhering strictly to the architecture defined in ADR 0147.

## Decision 2: TemporalCube Thread Safety & Tile Shared Ownership
- **Context**: `readChunk()` returns tile data while LRU eviction can occur concurrently.
- **Decision**: Store `std::shared_ptr<Tile>` internally in `mIndex` and `mLru`, and return `std::shared_ptr<const Tile>` or copy the chunk data if needed, synchronizing internal mutation using `std::mutex`.
- **Rationale**: Prevents dangling pointers when one thread evicts an item while another thread is reading its memory.

## Decision 3: Worker Node Lifetime in Python Plugin Adapter
- **Context**: `PythonPluginAdapter` stored `WorkerNode* m_workerNode;` raw pointer.
- **Decision**: Store `int m_workerId;` and query `PythonWorkerProcessPool::serverForWorker(m_workerId)` under `m_nodesMutex`.
- **Rationale**: Guarantees that if a worker process is killed or respawned, the adapter receives `nullptr` safely rather than dereferencing freed memory.
