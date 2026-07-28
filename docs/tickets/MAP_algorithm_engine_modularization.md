# Wayfinder Map: Algorithm Engine Modularization & TaskCenter Execution Alignment

## Destination

Decouple algorithm providers behind a uniform `AlgorithmProviderAdapter` interface seam, aligning `TaskCenter` to support provider-aware resource profiles, zero-copy shared memory intermediate result passing, and reactive DAG pipeline execution.

## Notes

- **Domain Glossary**: Consult [CONTEXT.md](file:///home/kevin/projects/exp-rs/CONTEXT.md) for Algorithm Engine, Task Center, Task Pipeline, Resource Throttler, Data Asset, Asset Lease, Derivation Record, and Python Plugin Host terms.
- **Architecture Vocabulary**: Apply `/codebase-design` deep module terms (**module**, **interface**, **depth**, **seam**, **adapter**, **leverage**, **locality**).
- **Relevant ADRs**: ADR 0001 (Algorithm Engine & Task Center), ADR 0012 (Atomic Algorithm Adapter), ADR 0014 (Out-of-Process Python Host), ADR 0016 (TaskCenter Deepening & Native DAG Engine).

## Decisions so far

- [TICKET-00: Scope & Destination](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_00_destination.md) — Locked destination as Architectural Specification & Seam Decision Map.
- [TICKET-01: AlgorithmProviderAdapter Seam](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_01_algorithm_provider_adapter_seam.md) — Providers auto-register via uniform `AlgorithmProviderAdapter` interface with `initialize()` / `discoverAlgorithms()` hooks.
- [TICKET-02: Heterogeneous Resource Throttling](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_02_heterogeneous_resource_throttling.md) — `TaskCenter`'s `ResourceThrottler` uses `ProviderResourceProfile` for in-process threads, Python worker pools, and CLI subprocesses.
- [TICKET-03: OutputCommitter & Shared Memory Seam](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_03_output_committer_shared_memory_seam.md) — Unified `OutputCommitter` handles `TaskTemporary` Data Assets, zero-copy POSIX shared memory (`SharedMemorySegment`), and `DerivationRecord` provenance.

## Not yet specified

- Provider dynamic unloading & hot-reloading lifecycle rules.
- LLM System Prompt tool schema export optimization for dynamically loaded Python providers.
- Inter-process crash isolation and recovery policies for C++ native plugin shared libraries (`.so` / `.dll`).

## Out of scope

- UI-level canvas rendering widgets (handled by `PipelineCanvasWidget` and `ActiveViewHost`).
- Third-party GDAL / OTB library source code modifications.
