# R2 review: post-D-landing deep review dossier + 13 filed issues (#959–#971)

## Summary

Full-scope follow-up to R0 (#957) after the 2026-09-13 landing wave (PRs #951–#958).
R0 covered `src/operators/**` + `pi/**` and was forbidden from filing issues —
its 2 surviving findings are filed here for the first time, plus 11 new findings
from the six landed feature PRs and cross-cutting drift.

## Filed issues

| Issue | Finding | Sev |
| --- | --- | --- |
| #959 | dialog help catalog bypasses i18n (whole help layer stays English) | P1 |
| #960 | offline gate global switch is a bare bool (data race) | P1 |
| #961 | lab copilot eval test uses bare setenv (MSVC compile failure) | P1 |
| #962 | class_mapping has no upper-bound check (R0 F-OPS-1) | P2 |
| #963 | io:reproject srcCrsOverride declared but never consumed (R0 F-OPS-4) | P1 |
| #964 | GF/HJ band-role wavelength notes deviate from published specs | P2 |
| #965 | cn import assumes TIFF band order equals table order | P2 |
| #966 | sun-elevation derivation provenance never reaches output | P2 |
| #967 | Gaussian spectral resampling skips monotonicity check | P2 |
| #968 | copilot teacher identity enforced by schema omission only | P2 |
| #969 | ADR 0146 reused by 9 files | P3 |
| #970 | .planning whitelist covers 17/39 track dirs | P3 |
| #971 | whole-raster NMS O(n²), no cancellation (R0 F-OPS-5) | P2 |

New labels created: `needs-triage`, `severity:P1`, `severity:P2`, `severity:P3`.

## Dossier

`review/DEEP_REVIEW_R2.md`: per-finding file:line + verbatim quotes, R0 re-verification
table (3 filed / 4 verified-fixed-or-mitigated), 12 retracted candidates, cross-cutting
observations. Every issue body carries a `Dedup:` line; build/test claims are
marked not-executed (no toolchain in session).

## Compliance

- `src/`/`tests/`: zero changes.
- No CI triggered, waited on, or cited. PR not merged by the author.

---
Track planning: `.planning/r2-deep-review/` (GOAL / DECISIONS / EVIDENCE / this body).
