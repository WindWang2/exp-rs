# MapSpec v4 migration — explainable constraint solving (Platform 7.0)

v4 is a **strict superset of v3**: every new field is optional, v3 documents
validate and compile unchanged. `upgradeMapSpec` re-stamps the envelope
version; no content migration is required.

## New constraint surface

Constraint items (in `constraints[]` with a solver `kind`) may now declare:

| field | type | default | meaning |
|-------|------|---------|---------|
| `hardness` | `"hard"` \| `"soft"` | `"hard"` | hard constraints participate in the convergence contract (v3 behavior); soft constraints are applied after the hard fixpoint and may yield to higher-ranked constraints |
| `priority` | integer 0..100 | `50` | rank within a phase: higher priority claims scarce geometry first |
| `weight` | number 0..1000 | `1` | soft constraints only — contributes to the weighted objective in the composition report; rejected at validation on a hard constraint |

```json
{ "id": "legend-under-map", "kind": "below", "items": ["map-1", "legend-1"],
  "gap_mm": 4, "hardness": "soft", "priority": 80, "weight": 3 }
```

## Solver behavior changes (all-hard documents are unaffected)

- Constraints are ordered **canonically** (hard before soft, priority
  descending, weight descending, declaration index ascending) before the
  relaxation runs. v3 was deterministic per input but could settle
  over-determined systems differently under declaration permutations; the
  chosen fixpoint is now declaration-order independent. The 6.0 report field
  semantics (`constraints_total/solved/passes/converged`) are preserved —
  for all-hard documents the values are unchanged.
- Soft constraints apply **after** the hard fixpoint in canonical order. A
  soft application that would break a hard constraint or a higher-ranked
  soft constraint is **reverted and reported**, never silently kept.
- Every unsatisfied hard constraint gets a **bounded unsat core**: the
  minimal conflicting subset found within a declared search radius
  (candidates ≤ 8, subset size ≤ 3, ≤ 32 simulations). Truncated searches
  are marked `"bounded": true` — a neighborhood witness, honestly bounded,
  never claimed to be a global minimum.

## New composition report fields

`resolveComposition(...).toJson()` (and the `composition` member of
cartography compose/evaluate responses) adds:

- `soft_total`, `soft_satisfied` — declared soft constraints and outcomes
- `satisfied_weight`, `violated_weight` — the weighted soft objective
- `decisions[]` — the bounded decisions ledger
  (`{cid, kind, outcome, reason, order}`; outcomes: `applied`, `rejected`,
  `anchor_wins`, `cycle`, `truncated`)
- `violated[]` — structured violations (`{cid, kind, reason}`); the legacy
  `unsatisfied[]` strings remain and now also cover unsatisfied soft
  constraints
- `unsat_cores[]` — `{constraint, core[], explanation, bounded?}`
- `fixpoint_policy` — the applied ordering policy string

## Validation

`validateMapSpec` rejects: unknown `hardness` values, non-integer or
out-of-range `priority`, non-numeric or out-of-range `weight`, and `weight`
declared on a constraint that is not `hardness: "soft"`.

## See also

- `docs/cartography/mapspec-reference.md` — full field reference
- `docs/cartography/preflight-rules.md` — how solver outcomes surface as
  preflight issues
