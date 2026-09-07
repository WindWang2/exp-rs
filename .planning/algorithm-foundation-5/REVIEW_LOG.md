# Review Log — Foundation 5.0

Lenses (phased, main agent + ≤2 fixed subagents, no per-lens agents):
1. Scientific correctness 2. Numerical stability/edge cases
3. Architecture/duplication 4. Performance/memory/cancellation
5. API/schema/docs claims (+ cross-platform C++, test adequacy as extras)

Format per finding:

```
ID: R<n>-L<lens>-<seq>
Severity: P0|P1|P2|P3
Location: path:line
Evidence: ...
Fix/Accepted-debt: ...
Verification: ...
```

Rules: P0/P1 must be fixed before PR (verification = failing-then-passing
test or diff re-review). P2 fixed or documented debt. Re-review the diff
after each remediation round — one pass is never enough.

## Round 1 — (pending: after Milestone A/B)

## Round 2 — (pending: after Milestones C–I)

## Final round — (pending: before PR)
