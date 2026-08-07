# ADR 0092: Spectral Library Matching Workbench (D10 first slice)

## Context

The D-series backend delivered spectral domain objects and kernels — the
spectral library (`SpectralLibrary`, ADR-0081), SAM/SID matching
(`spectral_classification.h`, ADR-0065/D6), wavelength-aware profiles
(ADR-0082) — but the DoD spectral workflow
`Spectral Profile ↔ Spectral Library → Matching / Analysis` had no assembly:
the Spectral Profile dock could display a pixel spectrum, yet nothing matched
it against a library. D10 ("integrated Spectral Analysis Workbench") needed
its first usable vertical slice.

## Decision

- **Backend service**: `SpectralLibrary::matchSpectrum(spectrum, library,
  nodata)` returns per-entry `MatchScore` (SAM angle in degrees + SID),
  ranked by ascending SAM angle (undefined angles sort last), skipping
  entries whose band count differs from the test spectrum. Pure and shared —
  the same service the UI, batch, and future Agent/MCP calls use.
- **Dialog**: `SpectralLibraryDialog` ("光谱库匹配") assembles the workflow:
  it takes the current Spectral Profile dock spectrum (values + wavelengths +
  labels), lets the user pick a spectral library JSON, runs the match, and
  shows a ranked table (排名/名称/物质/SAM°/SID); "保存当前谱到库" appends the
  current profile to the library and persists it (spectrum export). The
  dialog is a thin presentation adapter — `loadAndMatch()` and `runMatch()`
  are public so tests drive the headless widget state.
- **Entry point**: new "光谱分析" submenu under the 分析 menu with "光谱库匹配...",
  wired to `openSpectralLibraryDialog()` which pre-fills the spectrum from the
  profile dock when it has data.

## Consequences

- The spectral workflow is now end to end in the UI: click a pixel → profile →
  library → ranked SAM/SID matches → save profile back to the library — on top
  of the shared kernels (no duplicated algorithm code).
- Band-count-incompatible library entries are skipped with a clear status
  explanation rather than miscompared.
- Tests pin both seams: the pure `matchSpectrum` ranking (identical spectrum →
  angle ≈ 0, band mismatch skipped, empty inputs) and the dialog (match table
  content, malformed-library failure, save round trip persists 3 entries).
