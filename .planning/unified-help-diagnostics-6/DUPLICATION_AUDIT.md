# DUPLICATION AUDIT — Unified Help 6.0

Cases where the same fact is (or would be) maintained in more than one place, and
the resolution adopted by this track.

## Found at baseline

| # | Duplicated fact | Copies | Resolution |
| --- | --- | --- | --- |
| D1 | Command title/description | `command_defs.cpp` + menu assembly + palette + tooltips on QActions | Single definition stays in `CommandRegistry`; help layer only *adds* purpose/related. Menus/palette already read registry — no new copies. |
| D2 | Parameter type/range/default/enum | operator `schema()` and prose in dialogs/docs | Help derives these from schema at query time; `data/help` stores **only** additive knowledge (unit text beyond SI, recommended, trade-off). Drift test asserts stored knowledge never re-states machine facts (e.g. no "range 3–15" string for a param whose schema has a range). |
| D3 | Error code semantics | HarnessError category/retry + scattered prose in docs + ad-hoc dialog text | DiagnosticCatalog owns the human layer keyed by origin code; category/retry stay in `harness_error.*` and are *referenced*; retrySense fields in help content are checked against `retryClassForCode` in a drift test. |
| D4 | Shortcut listings | QAction bindings + docs | Generated shortcuts reference from registry (single owner). |
| D5 | Operator purpose/prereqs | `metadata()` JSON + `data/help` risk of copy | Rule: if `metadata()` already provides a fact, provider surfaces it; JSON may refine (中文) but drift test flags JSON entries that duplicate the *exact* English metadata string (sign of copy-paste). |
| D6 | Algorithm narrative | `docs/processing/*.md` + toolboxes | Generated pages *link* to hand-authored guides; never overwrite them. Generated reference carries only structured facts. |
| D7 | unavailability strings | `ContextRules::unavailabilityReason` + per-command lambdas (speckle) | New structured `explainFacts()` seam replaces ad-hoc lambda strings for raster/SAR case; command lambdas removed where they duplicated rules. |

## Avoided by construction

- No help content in C++ except fallback placeholders → content cannot fork between
  binaries and JSON.
- One Help ID namespace; registry rejects duplicates at registration and in tests.
- Parameter help IDs are *derived* from operator IDs → renames surface as drift
  failures instead of silent orphans.
