# R27-T04 Goal

Fix and deepen the Qt/QGIS remote-sensing workbench's reliability, interaction, and UI-performance defects on latest master — memory/data safety (P1), honest STAC/classification/ROI interactions (P2), GUI-thread IO (P2), atomic persistence (P2) — without new product directions, without duplicating merged R25/R26 PRs, delivering one PR after two independent adversarial reviews.

## Milestones
- **A (P1 safety closure)**: #1745 UAF, #1746 OOB write, #1747 project-loss-on-downgrade-refusal.
- **B (GUI async/perf)**: #1790 cross-section, #1791 magic wand, #1794 classification spectral recompute, off the GUI thread with generation/cancellation/QPointer discipline; p95 responsiveness measured, not asserted.
- **C (interaction correctness)**: #1788 append pagination, #1789 no-op signal + error-path state, #1792 atomic export with dest==src refusal, #1793 hardened JSON open, #1795 dual-channel mission persistence.
- **D (STAC SSRF at call site)**: #1765 GUI engages process private-network policy (existing geospatial API).
- **E (deepening within existing features)**: only completion of existing chains' honesty (states, errors, recovery) — no new frontends.
- **F (UX verification)**: Qt offscreen widget tests with real rasters; visual smoke marked NOT_EXECUTED if no display.
- **P3 sweep (#1805)**: capacity-gated low-risk items with genuine evidence.

## Non-goals (hard)
- No edits to `src/runtime/**`, `src/geospatial/**`, `src/agent_ops/**`, `src/app/agent_ops/**`.
- No new dependencies/ADR/product surfaces; no web frontends; no Python runtime.
- No blind `QtConcurrent` cross-thread access to Qt GUI objects — worker computes plain values, GUI lands via queued invoke with `QPointer`.
