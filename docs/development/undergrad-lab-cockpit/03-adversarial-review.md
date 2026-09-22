# Adversarial review — Undergraduate Lab Cockpit

## Attacks tried / mitigations

| Attack | Result | Mitigation |
|--------|--------|------------|
| Indeterminate verifier rendered as pass | Blocked by test 9 | `countsAsPass` false whenever status≠pass; empty→indeterminate |
| Corrupt session adopted | Blocked by test 12 | Unknown keys / bad schema / bad JSON → ok=false |
| Missing availability → Ready | Blocked | Course VM marks Unknown; readiness UNKNOWN items |
| Unknown operator → Ready | Blocked by test 6 | BLOCKED |
| Missing pack → Ready | Blocked by test 7 | BLOCKED |
| Scientific conflict ignored | Blocked by test 8 | BLOCKED |
| Prereq skipped | Blocked by test 3 | Module DAG → Unavailable + blocker text |
| Beginner/expert diverge truth | Blocked by test 15 | Same documents; mode only changes chrome |
| Autonomy L5 exam silently executes | Blocked by test 4 | exam ceiling L2; execution deny for student/lab |
| Grader leaks golden answers | Blocked by test 10 | Reasons = slugs only |
| Second ExperimentStore | Avoided by design | Session holds navigation refs only |
| Color-only status | Avoided | Text + icon token on every status |

## Fixes applied during review

1. Autonomy status projection now reads `allowed`/`limited`/`forbidden` arrays
   (real `sicnu.autonomy-status/1` shape), not a fictional capabilities map.
2. Timeline reflection detection parentheses cleaned (compiler warning).
3. Feedback aggregate: empty input → indeterminate, never pass.
4. Session unknown-key refusal (fail-closed).

## Residual risks

- UI compile against full shell not verified in this resource-limited lane.
- Availability probes in dock use a small allowlist for demo operators; production
  should inject `curriculum_registry_probe` (already exists in sicnu_agent).
