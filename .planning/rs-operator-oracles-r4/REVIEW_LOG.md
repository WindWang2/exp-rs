# REVIEW_LOG.md — Track 7 (R4) independent adversarial review

Reviewer: separate read-only subagent (1 of the 3 allowed slots), full
branch diff `origin/master..HEAD`, Standards axis + Spec axis. The
reviewer independently re-ran all 33 r4 tests (green) and hand-rederived
ten of the analytic truths (zonal moments, focal 341/8, apply_mask
3630/69, chain 1/(2(r+c)+7), change closed forms, terrain atan(2)/270,
stretch 127.5, derivative 0.01, SAR 9/4, continuum 0.75/11-12/0.875).

Verdict: **SHIP-WITH-FIXES** (0 P0, 0 P1, 7 P2, 4 P3).

## Pass 1 findings and dispositions

| # | sev | finding | disposition |
|---|---|---|---|
| 1 | P2 | digest gate counted 11 cases vs "≥12"/"12"/"14" in three documents | 12th TEST_CASE added (rs:spectral_similarity); matrix row corrected to "12 cases / 14 operator products"; ledger correction row appended |
| 2 | P2 | matrix commit map unfilled | filled with real SHAs (be1b0f8ee, b6225cf35, bbf5e7ee0, c6e90edea, f4a19336e, 9003c4279, b1e33b126) |
| 3 | P2 | REVIEW_LOG committed as placeholders | this file, filled at review close |
| 4 | P2 | image_enhancement filter/speckle sentinel-as-data unaudited; NaN declaration vacuous for those paths | matrix row 24 note + DECISIONS D4b backlog entry (no source change; declaration is accurate for the NaN-input holes those paths DO write) |
| 5 | P2 | gate regex narrowed without neighbor evidence | DECISIONS D9 + EVIDENCE §4 disclosure; full-regex run recommended in CI pre-merge |
| 6 | P2 | WP-E missing two named refusal classes | added: apply_mask mask-CRS mismatch (UTM vs WGS84 fixture) and qa_mask missing-band-roles; DECISIONS D10 records the dtype-matrix narrowing |
| 7 | P3 | ledger round-3 counts stale/self-contradictory | append-only correction row in ledger |
| 8 | P3 | DECISIONS D3 stale (pca listed as defect) | D3b correction appended |
| 9 | P3 | similarity single-sentinel false-mask risk | matrix row 105 note + D4b backlog |
| 10 | P3 | per-tile masked-buffer allocation in derivative | hoisted above the tile loop; no-sentinel path unchanged (verified bit-exact) |
| 11 | P3 | D7 cross-process determinism overstated | D7b reword appended |

## Pass 2 (verification of dispositions)

Filled after the disposition commit: rebuild + `ctest -R "^r4::" -j1`
double run with the expanded suites — **35/35 passed, exit 0, two
consecutive passes** (15.56 s / 13.18 s; per-suite counts in EVIDENCE.md
§4). Reviewer re-verification: pass 2 requested on the disposition diff
(REVIEW_LOG/DECISIONS/matrix updates + 3 test additions + buffer hoist);
findings appended below if any.

**Pass 2 verdict: SHIP-WITH-FIXES (residuals all P3, dispositioned in the
close-out commit; reviewer: "merge after that one commit — no further
review round needed").** Pass 2 independently re-executed all 35 cases
green and verified every pass-1 disposition landed. Residuals R1–R7:
stale digest row in the tracked matrix doc (R1), untracked PR_BODY.md
(R2), severity-split misstatement 7/4→6/5 (R3), ledger row-5 count slip
(R4), malformed markdown in matrix rows 24/105 (R5), under-asserted
qa_mask refusal (R6 — now pins InvalidParameter + "QA band" message),
D10 dtype-framing overstatement (R7).
