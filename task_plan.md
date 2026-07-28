# Task Plan: Algorithm Engine Modularization & TaskCenter Alignment Map

## Goal
Chart and specify the architectural decision map for Algorithm Engine modularization and `TaskCenter` execution alignment, defining provider adapter seams, heterogeneous resource throttling, zero-copy shared memory transports, and reactive DAG task pipeline scheduling.

## Current Phase
Phase 5: Delivery

## Phases

### Phase 1: Wayfinder Scope & Destination Charting
- [x] Name the destination: Architectural Specification & Seam Decision Map for Algorithm Engine Modularization & TaskCenter Alignment
- [x] Create Wayfinder Map file [MAP_algorithm_engine_modularization.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_algorithm_engine_modularization.md)
- [x] Create [TICKET_00_destination.md](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_00_destination.md)
- **Status:** complete

### Phase 2: Frontier Decision Mapping & Grilling
- [x] Decision 1: Provider Auto-Registration Seam (`AlgorithmProviderAdapter`) -> [TICKET_01](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_01_algorithm_provider_adapter_seam.md)
- [x] Decision 2: Heterogeneous Resource Throttling (`ProviderResourceProfile`) -> [TICKET_02](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_02_heterogeneous_resource_throttling.md)
- [x] Decision 3: Output Artifact & Shared Memory Seam (`OutputCommitter`) -> [TICKET_03](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_03_output_committer_shared_memory_seam.md)
- **Status:** complete

### Phase 3: TaskCenter Native DAG Engine Implementation
- [x] Implement native `submitPipeline` & `submitPipelineJson` in `TaskCenter`
- [x] Refactor `WorkflowSessionController` into reactive UI observer
- [x] Record ADR 0016 in `CONTEXT.md`
- **Status:** complete

### Phase 4: Verification & Ticket Indexing
- [x] Create ticket files in `docs/tickets/`
- [x] Link all closed decision tickets in Wayfinder Map
- **Status:** complete

### Phase 5: Delivery
- [x] Deliver Wayfinder Map and decision tickets to user
- **Status:** complete

## Decisions Made
| Decision | Ticket Link | Summary |
|----------|-------------|---------|
| Destination Scope | [TICKET-00](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_00_destination.md) | Locked Architectural Specification & Seam Decision Map. |
| Provider Adapter Seam | [TICKET-01](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_01_algorithm_provider_adapter_seam.md) | Uniform `AlgorithmProviderAdapter` interface for provider discovery. |
| Resource Throttling | [TICKET-02](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_02_heterogeneous_resource_throttling.md) | Provider-aware `ResourceThrottler` for in-process, IPC pool, and sub-processes. |
| Output Committer | [TICKET-03](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_03_output_committer_shared_memory_seam.md) | Unified `OutputCommitter` handling zero-copy shared memory and `DerivationRecord` provenance. |
