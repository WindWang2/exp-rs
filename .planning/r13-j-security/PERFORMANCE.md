# Performance Ledger: R13-J Security and Concurrency Hardening

## Performance Guidelines
- Hostname resolution: Only invoke `getaddrinfo()` if the hostname is not already recognized as an IP literal or localhost string. Cache negative lookups where appropriate or rely on OS-level nscd/systemd-resolved.
- Mutex contention: In `TemporalCube`, lock durations are kept strictly to map/list updates; memory allocation/reading is minimized under lock.
- Python worker drain: Avoid holding the lock during blocking I/O; acquire lock to detach node/server and drain safely.
- VRAM session pool eviction: O(N) scan over active sessions on allocation only when threshold is exceeded.
