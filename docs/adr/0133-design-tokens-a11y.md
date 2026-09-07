# ADR 0133: Design Token Layer & QSS Parity Contract (UX 4.0, Milestones F/G)

- Status: Accepted (Desktop Workbench & Unified UX 4.0 goal)
- Context: the Canopy Lab design tokens existed only as a header comment in
  each QSS file, while C++ code re-derived colors locally: RsJobPanel and the
  georeferencer task list each kept their own task-status palette (with a
  divergent green family), `applyDarkPalette` hand-duplicated the dark token
  set as QPalette literals, and theme detection was reimplemented per file.
  QSS stayed hand-maintained (no codegen), so C++ and QSS could drift
  silently.
- Decision:
  1. **One C++ token owner**: `src/app/design_tokens.h` (`SicnuUi::Tokens`)
     defines semantic colors for both themes, the shared task-status palette,
     spacing/icon/type scales, and the single `themeIsDark()` probe.
  2. **Parity is tested, not assumed**: `test_theme_selector_parity.cpp` now
     asserts the C++ constants and the QSS token headers stay in sync for both
     themes. Change QSS and tokens in the same commit.
  3. **Convergent consumers**: RsJobPanel's `statusColor`, the georeferencer
     task list, and `applyDarkPalette` read tokens (the known consumers are
     pinned by the parity test). The pipeline editor is a documented
     exception — a deliberately dark slate graphics scene in both themes with
     its own vibrant badge palette; by convention its colors must not leak
     into panel/table code (review-enforced, not test-enforced).
  4. **Keyboard hygiene**: the shell action host may not claim the same key
     sequence twice (`test_shortcut_conflicts.cpp`); conflicts need an
     explicit, commented whitelist entry.
- Consequences: one dark palette instead of three hand-kept copies; theme
  switches preserve semantics across panels; new code has a non-bypassable
  path for colors (tests pin the known consumers); keyboard conflicts fail
  review at test time instead of surprising users.
