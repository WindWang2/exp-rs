# Specification: Task Center Execution and Georeferencing Session Deepening

## Problem Statement

Remote-sensing users can start work from several places, but the application does not consistently present that work as one Algorithm Task. Some callers create a Task Center record while separately submitting to JobEngine; others submit directly and translate completion, cancellation, progress, and cleanup themselves. This splits task lifecycle state across the Task Center, workspace-specific task views, and individual windows. Priority, Task Pipeline gating, retry, logs, cancellation, and result loading can therefore differ by entry point.

Image Registration has a related usability and reliability problem. Image-to-Image and Image-to-Map share registration behaviour, but that behaviour is mixed with window construction, QGIS canvas interaction, docks, map tools, task lifecycle work, and persistence. Changes to Ground Control Points, fit readiness, residuals, or warp submission require knowledge of both window variants and their broad subclass protocol.

## Solution

Make the Task Center the sole caller-facing execution module for every UI-started Algorithm Task. It owns task lifecycle, queueing, priority, Task Pipeline gating, logs, cancellation, result handling, and task-view projections. JobEngine becomes an internal execution adapter; UI callers no longer submit to it directly. The Task Center supports both registry-backed operators and specialised callable work behind the same submission interface.

Deepen Image Registration around a shared Georeferencing Session. The session owns source/destination context, Ground Control Point pairing, fit readiness, residual state, immutable warp snapshots, warp lifecycle coordination, and persisted workspace state. Image-to-Image and Image-to-Map remain adapters that provide their different canvas and destination-pick interactions.

## User Stories

1. As a remote-sensing analyst, I want every UI-started operation to appear as an Algorithm Task in the Task Center, so that I have one reliable place to monitor work.
2. As a remote-sensing analyst, I want task status, progress, logs, cancellation, and result state to agree wherever a task is shown, so that I can trust what the application reports.
3. As a remote-sensing analyst, I want a workspace task list to show the relevant Algorithm Tasks, so that I can stay focused without creating a second task lifecycle.
4. As a remote-sensing analyst, I want cancelling a task from either the Task Center or a workspace projection to cancel the same Algorithm Task, so that cancellation is predictable.
5. As a remote-sensing analyst, I want queued work to receive the same priority and Task Pipeline rules regardless of which window started it, so that scheduling is fair and comprehensible.
6. As a remote-sensing analyst, I want a completed task’s output and error information to be available consistently, so that I can load results or diagnose failures without reopening the originating window.
7. As a remote-sensing analyst, I want specialised work such as a georeferencing warp or OBIA operation to run through the same Task Center, so that specialised tools do not lose task-management features.
8. As a remote-sensing analyst, I want retry and cancellation behaviour to be defined at the Algorithm Task level, so that it is not dependent on which UI initiated the work.
9. As an application developer, I want one Task Center interface for operator-backed and callable execution, so that a new UI flow does not need to recreate JobEngine and Qt completion mechanics.
10. As an application developer, I want JobEngine details hidden behind the Task Center seam, so that worker implementation changes do not force every UI module to change.
11. As an application developer, I want terminal-state delivery and connection lifetime management concentrated inside the execution module, so that closing a window cannot leave ad hoc callbacks behind.
12. As an Image Registration user, I want Image-to-Image and Image-to-Map to apply the same Ground Control Point and fit rules, so that the mode I choose does not change registration semantics.
13. As an Image Registration user, I want Image-to-Image to retain its reference-image interaction and Image-to-Map to retain its map-coordinate interaction, so that each window matches its task.
14. As an Image Registration user, I want a warp to use the settings and Ground Control Points visible when I started it, so that later edits cannot change queued work.
15. As an Image Registration user, I want fit readiness and residual feedback to update consistently after Ground Control Point changes, so that I know when a warp can run.
16. As an Image Registration user, I want a completed, failed, or cancelled warp to update both the Task Center and the current workspace view consistently, so that I can act on the outcome confidently.
17. As an Image Registration user, I want closing one window to preserve its own Georeferencing Session without affecting the other window, so that concurrent registration work remains independent.
18. As an application developer, I want to test Georeferencing Session behaviour without constructing a full QGIS window, so that registration rules have a focused and stable test surface.
19. As an application developer, I want Image-to-Image and Image-to-Map tests to focus on their canvas and destination-pick adapters, so that UI tests do not duplicate registration-session coverage.
20. As an application maintainer, I want the architecture to honour the existing decision that JobEngine is an internal Task Center adapter, so that future changes do not recreate parallel execution paths.

## Implementation Decisions

- The Task Center is the sole caller-facing execution module for UI-started Algorithm Tasks.
- No UI module submits directly to JobEngine after migration. Any low-level submission helper becomes internal implementation of the Task Center or is removed.
- JobEngine is an internal execution adapter of the Task Center, consistent with ADR-0006. It may execute both registry-backed operators and specialised callable work.
- The Task Center submission interface captures the Algorithm Task’s execution choice, immutable input data, scheduling metadata, cancellation behaviour, and terminal-result contract without exposing the concrete worker adapter to callers.
- Task Center owns lifecycle transitions, priority, Task Pipeline gating, logs, results, retry, cancellation, and output-loading eligibility.
- A workspace-specific task list is a read-only projection of Algorithm Tasks relevant to that workspace. It may route user actions to Task Center, but it cannot own independent lifecycle state.
- Existing direct submission paths migrate incrementally, beginning with UI flows that currently dual-write Task Center and JobEngine state. A migration is complete only when the caller crosses the Task Center seam once.
- RsJobRunner-style terminal notification and connection-lifetime mechanics are internal to the Task Center execution module; they are not a second UI execution interface.
- The Georeferencing Session is the shared registration module for Image-to-Image and Image-to-Map. It owns source/destination context, Ground Control Point pairing, transform fit readiness, residual state, immutable warp snapshots, lifecycle coordination, and persisted workspace state.
- Image-to-Image and Image-to-Map are adapters over the same Georeferencing Session. Their canvas layout, available tools, reference-image versus map-coordinate picking, and map integration remain adapter responsibilities.
- Starting a warp creates an immutable warp snapshot from the Georeferencing Session. Task Center executes that snapshot and later reports the terminal outcome back to the session and its workspace projection.
- A Georeferencing Session is independent per window instance. Closing or changing one session cannot alter another session’s Ground Control Points, fit state, destination context, or queued snapshots.
- The design deliberately does not fold Classification workflow authority or its runtime mirroring into this scope. That separate dual-write concern remains available for a later deepening effort.

## Testing Decisions

- Good tests observe external behaviour through the highest available seam. They assert Algorithm Task lifecycle and Georeferencing Session outcomes; they do not assert worker selection, private callback wiring, or widget implementation details.
- Task Center tests verify that operator-backed and callable work enter the same lifecycle, that priority and Task Pipeline rules apply before execution, and that progress, logs, cancellation, retry, success, failure, and results are exposed consistently.
- Migration tests verify that a UI flow creates one Task Center-owned Algorithm Task rather than parallel Task Center and JobEngine records.
- Workspace-projection tests verify that a filtered task view reflects its Task Center Algorithm Task and routes cancellation or result actions back through Task Center without local lifecycle duplication.
- Georeferencing Session tests verify Ground Control Point pairing, fit readiness, residual updates, immutable warp snapshot creation, session independence, and terminal outcome handling without constructing either window.
- Image-to-Image and Image-to-Map adapter tests verify their distinct canvas arrangement, destination-pick behaviour, and mapping to the shared Georeferencing Session. They do not duplicate the session’s fit or warp-lifecycle coverage.
- Existing Task Center, JobEngine, dual-window georeferencing, task-list, and workflow-runtime tests provide prior art. New tests should preserve their behavioural intent while moving assertions upward to the Task Center and Georeferencing Session interfaces.

## Out of Scope

- Replacing JobEngine with a different worker system.
- Distributed or cloud execution.
- Persisting Task Center history across application restarts.
- Reworking Task Pipeline semantics, priority policy, or resource throttling.
- Redesigning the visual layout of the Task Center or Image Registration windows beyond what the new seams require.
- Unifying Classification workflow authority and runtime mirroring.
- Changing the user-visible Image-to-Image versus Image-to-Map workflow beyond preserving their existing adapter-specific interactions.

## Further Notes

- The confirmed primary test seams are the Task Center interface and the Georeferencing Session interface.
- The specification restores the architectural intent of ADR-0006 rather than superseding it.
- The Georeferencing Session term is recorded in the project domain glossary.
- This is a cross-cutting refactor. Follow-up tickets should preserve the blocker order: establish the Task Center execution seam before migrating callers; establish the Georeferencing Session before moving both window adapters onto it.
