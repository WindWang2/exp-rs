# docs(agents): /goal and /loop command system review, templates and vocabulary

## Summary

R1 · Prompt & Command Hygiene Review（/goal 与 /loop 指令体系审查与优化）。

`/goal` has driven 24 real tracks in this repo, but its conventions were never written down —
they lived in 24 differently-shaped GOAL.md files, in-session goal briefs, and one changelog line.
This track reviews the whole command system, lands its de facto conventions as three
self-contained documents, and fixes every broken reference in the agent-rule layer.

## Deliverables

- **`docs/agents/goal-template.md`** — single source of truth for `/goal`: parameter header,
  copy-paste skeleton, nine-phase budget scheme, 7 mandatory Autonomy-defaults classes,
  10-step PR runbook (incl. `.planning` whitelist self-check, push-failure fallback,
  post-review continuation), existence assertions, hard checklist.
- **`docs/agents/loop-template.md`** — single source of truth for `/loop` (explicitly marked
  as a derived convention: zero precedent in this repo): Trigger / Checkpoint / Push right /
  Brief from `loop-me`, cross-run cursor state, anti-pattern table.
- **`docs/agents/command-vocabulary.md`** — the `/goal` vs `/loop` vs `wayfinder` vs `grilling`
  decision tree ("will this need to be done again?"), 3 real historical track classifications,
  shared P0–P3 severity vocabulary.
- **`review/GOAL_MATRIX.csv`** — exhaustive 24-track × 24-convention matrix with file:line
  evidence per cell (plus the 12 track dirs that have no GOAL.md at all).
- **`review/PROMPT_DEFECTS.md`** — 28 defects across five lenses, each with a verbatim quote;
  de facto convention synthesis; 4 retracted candidates (false-positive rate 4/28 ≈ 14.3%).
- **`review/SKILL_INVENTORY.md`** — ground truth on skills: `karpathy-guidelines` missing,
  `planningwithfiles`/`gstack` user-level only, `matt` = ask-matt (now mirrored on both sides).
- **`review/SKILL_MIRROR.md`** — `.agents/skills/` × `.claude/skills/` mirror state (37 shared,
  byte-identical; 13 vendor skills one-sided) with rules going forward.
- **`.agents/AGENTS.md`** — dead `karpathy-guidelines` link removed (provenance now points at
  the 2026-08-03 CHANGELOG entry), C++17→C++20 (CMakeLists.txt is authoritative), cross-ref to
  CLAUDE.md, unattended-mode adaptation note.
- **`CLAUDE.md`** — Quick Commands rewritten to CMakePresets + `-j2` (the old
  `make -j$(nproc)` violated the repo's own hard resource cap and pointed at a nonexistent
  build path); broken `.agents/vendor/` reference removed; "mirrored" wording aligned with
  measured reality; cross-ref to AGENTS.md.
- **`.gitignore`** — one whitelist block for this track's planning files (mechanical
  necessity; the same mechanism previously lost a whole track's planning files — defect D-005).

## Notable findings

1. No template/vocabulary existed: 7 GOAL.md files cite an out-of-repo "goal brief §N";
   R0's binding GOAL text was never committed; 12/36 track dirs have no GOAL.md.
2. `.gitignore:119` silently swallows new tracks' `.planning/` files (spatial-scientist-harness-8
   lost all original planning files this way).
3. AGENTS.md and CLAUDE.md (the two runtime rule files) had zero cross-references and had
   already drifted into contradictions (C++17 vs C++20; `-j2` vs `-j$(nproc)`).
4. 24/24 GOAL.md files reference zero skills — the command system and the skill system
   never met; template now wires them together with a precedence rule.
5. `/loop` has zero landing: no precedent, no `workflows/` dir — documented as a derived
   convention, not a de facto one.

## Cross-review

Two read-only subagents: A (archaeology — independently re-derived conventions; corrected one
synthesis item, contributed 3 defects) and B (adversarial — drove an imaginary track through the
new templates and found 30 holes; all 30 fixed, none left requiring user questions).
Adjudications: `.planning/prompt-command-hygiene-review/REVIEW_LOG.md`.

## Compliance

- `src/` and `tests/`: **zero changes** (see diff stat below).
- No build, no tests executed (documentation-only track). No CI triggered, waited on, or cited.
- No remote issues created. PR not merged by the author.

Diff stat vs base (origin/master @ 60179408): 16 files changed, 2088 insertions(+), 7 deletions(-).
Paths touched: `.agents/AGENTS.md`, `.gitignore`, `.planning/prompt-command-hygiene-review/*` (8 files), `CLAUDE.md`, `docs/agents/*` (3 new), `review/*` (4 new).
`git diff --name-only origin/master...HEAD | grep -cE "^(src|tests)/"` → **0**.

---
Track planning: `.planning/prompt-command-hygiene-review/` (GOAL / PLAN-facade via EXECUTIVE_SUMMARY / DECISIONS / EVIDENCE / REVIEW_LOG / PR_BODY).
Executive summary: `.planning/prompt-command-hygiene-review/EXECUTIVE_SUMMARY.md`.
