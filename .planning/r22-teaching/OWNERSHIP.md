# OWNERSHIP — r22-teaching

## This track writes (allowed scope, from task charter)

- src/teaching/**, src/teaching_admin/**, src/app/teaching_admin/**
- src/lab/** (EXCEPT the infra include-only fix in src/lab/session_state.cpp — infra lane owns that; NaN/Inf behavioral fix IS in scope)
- src/grader/**, src/lab_pack/**, src/experiment_studio/**, src/faultlab/**
- src/verification/**, src/verify/** (EXCEPT infra system-header rename), src/verify_adapters/**
- src/agent/output_verifier.*
- tests/test_r22_teaching_* (new tests only)
- tests/CMakeLists.txt — append-only shared registry, small blocks, git-blame checked before edit

## Read-only for this track (owned by sibling open PRs)

- #1674 (r21-E): src/agent/output_verifier.*, src/experiment_studio/live/studio_live.cpp,
  src/faultlab/*, src/verify/CMakeLists.txt, src/verify_adapters/lab_verify_composer.*
  → we do NOT rewire verifier production paths (F3 is verification/review of #1674's approach,
  any gaps recorded in CROSS_TRACK_FINDINGS.md instead of editing its files this round).
- #1673/#1675/#1676/#1677: no teaching overlap.
- scripts/build_offline_bundle.sh/.cmd — 07 track owns; F2 aligns interface, records findings.

## Not in scope

- tests/test_e2e_open_issues.cpp (owned elsewhere; #1558 already fixed anyway)
- Pi/CLI core, packaging shell scripts, CI workflows.
