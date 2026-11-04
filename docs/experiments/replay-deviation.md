# Replay deviation report v2 — typed reasons and remediation

R25-B feature 3, `src/experiment/replay_deviation.h`.

The analyzer (M7) answers "what changed?" after a replay with honest
verdicts (`identical` / `equivalent` / `deviated` / `incomplete` —
identity-pin-driven, unchanged since M7/#1547). Schema **v2** keeps every
v1 field byte-identical and adds one thing the flat strings could not
carry: **typed deviation entries**.

## `deviation_details[]`

One entry per line in the legacy `deviations` list:

| field | meaning |
|---|---|
| `dimension` | the pin/diagnostic that differs (`dataset`, `split`, `model`, `algorithm`, `config`, `seed`, `environment`, `artifacts`, `runtime`, `result`) |
| `domain` | `data` \| `model` \| `experiment` \| `environment` — which side of the experiment owns the deviation |
| `identity_pin` | true when this dimension participates in the verdict |
| `detail` | what differs (the v1 text) |
| `remediation` | the actionable next step (never empty) |

Domain attribution: dataset/split → `data`; model → `model`;
config/seed/algorithm and the artifacts/runtime diagnostics →
`experiment`; environment → `environment`.

Remediations are concrete, per dimension: pin the original dataset
version; regenerate the split from the recorded manifest/seed; resolve the
same model identity@version + content digest; diff the canonical parameter
JSON; re-run with the recorded seed; for `result` (fingerprints differ
under Strict determinism with all pins equal) — audit threading/vendor
kernels or declare the true determinism grade.

## Compatibility

- `schema_version` bumps to `2`; `deviations` (strings), `verdict`,
  `pin_comparison`, `metric_delta`, `environment_drift`, `evidence_gaps`
  keep their v1 names, spellings and semantics.
- v1 consumers that ignore unknown fields are unaffected.

Tests: `tests/test_r25_b_replay_deviation_v2.cpp` — domain attribution for
dataset/model/config/environment deviations, non-empty remediations, v1
string spellings preserved, identical replays produce no entries, unknown
runs still refuse typed, JSON round-trip.
