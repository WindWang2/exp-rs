# Performance Ledger: R13-K JsonCpp Type Safety & Exception Defense

## Resource Budgets & Invariants
- Build concurrency: `-j2` (strictly enforced via global lock `/tmp/exp-rs-agent-locks/global-build.lock`)
- Test concurrency: `-j1`
- Display environment: `QT_QPA_PLATFORM=offscreen`

## Overhead Analysis
- **Inline type checks**: Using `isString()`, `isBool()`, `isInt()` performs a single enum comparison (`type() == ...`) in JsonCpp, which is $O(1)$ and eliminates the expensive stack unwinding that occurs when an exception is thrown.
- **Exception barriers**: Placed only at API entry points (`canonicalJson`, `armRepairApproval`, `fromJson`), with zero overhead on the non-exceptional happy path on x86_64 (zero-cost DWARF exception tables).
