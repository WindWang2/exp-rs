# REVIEW_R1 — r22-teaching (independent adversarial subagent, 2026-10-26)

Scope: commits 1610c9b75 (RED) + 70afbb6a8 (GREEN) vs master 911e0b17a.

## VERDICT: REJECT

## FINDINGS

1. **P1 — #1694 digest still blind to rules edits on the dock's primary path.**
   `teaching_admin_dock.cpp:395` passes the lab ID; `gradingRulesFingerprint()` fingerprints
   content only for an existing file, so a lab id degenerates to constant `labid:<id>`. The
   real authority is `data/labs/grading/<id>.rules.json` resolved by the CLI
   (`src/agent/output_verifier.cpp:900-923`: SICNU_LAB_RULES_DIR, then source tree).
   Teacher edits that file → digest unchanged → stale adoption persists. Tests only cover
   the rules-path configuration, never lab-id→resolved-file.

2. **P1 — #1689 word-boundary guard false-positives on Chinese-name prefix overlap.**
   ASCII-only boundary classes: cohort {张三, 张三丰} → 张三's pattern matches inside
   张三丰's own student_id in their clean pack → production gate withholds a legitimate
   pack. Header contract "substring adjacency does not trip it" is false for CJK.
   Secondary: duplicate ids in a cohort are excluded by `id == studentId`, so leaks
   between identically-named students stay invisible (inherent ambiguity).

3. **P2 — `regradeTraceability` now leaks run-metadata into the outcome digest.**
   New `checkpoint_invalidated` field is not removed before hashing, breaking the
   documented "resumed restart and fresh run share the digest" invariant.

4. **P2 — `sampledContentSha256` interior blind spot.** 4–8 MiB files hash head only;
   >8 MiB hash head+tail only. Same-size interior rewrite of the grader binary keeps the
   identity → stale adoption. Inconsistent with artifactStamp's #1520 interior-sampling
   rationale; `sha256OfFile` (streamed) already exists in json_util.h:45.

5. **P3 — version whitelist admits Latin-1 letters** (`c.toLatin1() != 0` passes é/ü/ñ),
   contradicting the documented `[0-9A-Za-z._-]`.

6. **P3 — comment/code mismatch**: "Size+name keeps it deterministic" but only size is emitted.

7. **P3 — `;` `,` `=` survive path validation** although cmd.exe re-tokenizes them for
   `.cmd` targets; `repoRoot` flows into the same argv unvalidated.

8. **P3 — per-pack regex recompilation on the GUI thread** (O(N²) alternation compiles in
   `buildGuardedFeedbackPacks`).

9. **P3 — invalidation message asserts rules/grader changed** even for a pure
   first-run-after-upgrade digest mismatch.

Held (verified sound): #1678 both boundaries + no other writers of questionAnswers/.numeric;
digest computation consistency (single in-memory cfg); 2-arg guard delegation compat;
legal-argv expectations; no downstream "ui-1" pin; loadBatchReportFromFile ignores unknown
fields; no catastrophic backtracking; GUI-thread widget reads; RED evidence honest.

## TEST-GAP notes (1-8) → all addressed in the R1-fix commit (see LOOP_LEDGER R8).
