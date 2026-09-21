# Integrating the Unified Scientific Verifier

This is a how-to for callers. It assumes you have read
[`README.md`](README.md) or [ADR 0172](../adr/0172-unified-scientific-verifier.md)
and know what `Indeterminate` means.

## 1. Build a spec

A spec is the *declaration*. It says what must be true, in the closed
vocabulary, and carries no values — values come from providers at run time.

```cpp
#include "verification/spec.h"

using namespace sicnu::verification;

VerificationSpec spec;
spec.specId = "ndvi-product";
spec.specVersion = "1";

VerificationCheck shape;
shape.id = "artifact.shape";
shape.kind = checkKindToWire( CheckKind::ArtifactShape );   // wire string
shape.title = "the product is a raster with the declared grid";
shape.subject = SubjectRef{ "artifact", "ndvi.tif" };
shape.params["kind"] = "raster";
shape.params["size"]["width"] = 512;
shape.params["size"]["height"] = 512;
shape.failureCode = failure_codes::kArtifactGridMismatch;

spec.checks.push_back( shape );

// Ask for structural validation before running. It answers "can every lane
// downstream read this?", not "are these expectations scientifically sound?".
const std::vector<std::string> problems = validateSpec( spec );
if ( !problems.empty() )
{
    // A structurally broken spec is Indeterminate/SPEC_INVALID, not a crash.
}
```

**Do not** set `spec.checks` from a filter without checking for the empty
result. An empty spec is `VERIFY.NO_CHECKS` / `Indeterminate` by design, but it
is almost never what you meant.

## 2. Wire providers

Providers are the *only* way the verifier learns anything. There are five, and
each returns `Availability`, never a bare value:

```cpp
#include "verification/providers.h"

class MyArtifactProvider : public ArtifactProvider
{
  public:
    Availability describe( const std::string &ref, Json::Value &facts,
                           std::string &reason ) const override
    {
        if ( !exists( ref ) )
        {
            reason = "no such artifact: " + ref;
            return Availability::Missing;      // -> Indeterminate, NOT an empty fact set
        }
        if ( !readable( ref ) )
        {
            reason = "permission denied";
            return Availability::Refused;      // -> Indeterminate
        }
        facts = readFacts( ref );              // keyed by the kFactKeys vocabulary
        return Availability::Found;
    }
};

VerificationInputs inputs;
inputs.artifact = &myArtifactProvider;   // unset members are Missing, never "true"
inputs.sourceId = "ndvi-product-pipeline";
```

Three rules that are easy to get wrong:

1. **Return `Missing`, not an empty object.** `facts = Json::Value{}` with
   `Availability::Found` claims "I looked and there is nothing there", which is
   a statement, not an absence of one.
2. **Use the `kFactKeys` vocabulary.** The keys are the harness's closed set
   (`kind`, `numeric_domain`, `crs_authid`, `size`, `extent`, …). A private
   spelling means the checker will not find the fact.
3. **`sourceId` is recorded on every evidence record.** Set it to something a
   person could act on — "who told you that?" is a question every `Fail` and
   `Indeterminate` should be able to answer.

## 3. Run it

```cpp
#include "verification/check_runner.h"

const VerificationReport report = runSpec( spec, inputs );   // or runSpecFlat()
```

`runSpec` takes an optional `NodeCheckMap` (`nodeId -> check ids`) when your
spec covers a multi-node plan; without it the report has no node level and the
task outcome rolls up from the checks directly.

## 4. Read the report

```cpp
switch ( report.status )
{
    case CheckStatus::Pass:
        // Evidence was available and every expectation held. Ship it.
        break;

    case CheckStatus::Fail:
        // Something is definitively wrong. `report.outcome.failureCodes` names it
        // and `report.outcome.blockingNodes` says where.
        break;

    case CheckStatus::Indeterminate:
        // We could not conclude. Read `report.outcome.failureCodes` to find out
        // WHY, then obtain the missing evidence or decide explicitly. Do not
        // treat this as "probably fine": that is the bug this module exists for.
        break;
}
```

Useful fields:

| Field | What it tells you |
| --- | --- |
| `report.status` | Flat lattice roll-up over all results |
| `report.outcome.status` | Level-2 task outcome (authoritative when nodes exist) |
| `report.outcome.blockingNodes` | What is stopping the task, in roll-up order |
| `report.outcome.replanClasses` | `retry` / `replan` / `abort` per code |
| `report.results[i].evidence` | What was observed, expected, and where it came from |
| `report.budgetUsage.exceeded` | Whether the run was truncated — **check this** |
| `report.specDigest` | Content address of the spec that produced this report |

**Always check `budgetUsage.exceeded`.** A truncated run and a complete run must
never look the same; if the budget was hit, some checks are `Indeterminate` for
that reason and the report is not a complete verification.

## 5. Present it

Use the renderer that matches your audience. Both derive their status from the
**level-2 structure first**, so a flat optimistic reading cannot leak through.

```cpp
#include "verification/render_teaching.h"
#include "verification/render_agent.h"

const TeachingView teaching = renderTeaching( report );
// teaching.verdict ∈ {"trustworthy", "not_trustworthy", "unverified"}
// teaching.headline is one actionable sentence; teaching.reasons is never empty.

const AgentView agent = renderAgent( report );
// agent.status ∈ {"pass", "fail", "indeterminate"}
// agent.replanClasses / agent.suggestedActions drive what to do next.
```

Note `"unverified"` ↔ `"indeterminate"`: the same verdict, in the vocabulary each
audience already uses. They are kept as synonyms deliberately — a student and an
agent looking at the same result should never be told two different things.

## 6. Compose from packs

If you are checking a class of products rather than one product, start from a
pack instead of typing checks:

```cpp
#include "verification/packs_builtin.h"

VerifierPack structural = structuralPack();
DerivedPack derived = scientificContractFor( "ndvi" );   // projected from the registry

if ( derived.indeterminate )
{
    // Unregistered operator: the pack carries one visibly unevaluable check so
    // the absence reports as UNSUPPORTED_CHECK_KIND instead of "no checks".
}

std::vector<VerifierPack> packs{ structural, derived.pack };
const ComposeOutcome outcome = composePacks( packs, PackConflictPolicy::Reject );

if ( !outcome.ok )
{
    // NOTHING is returned on refusal — a half-merged set would look like success.
    // outcome.failureCode tells you whether it was a conflict or an empty result.
}
```

Under `PackConflictPolicy::Override`, the **first** declaration wins (packs are
merged in pack-id order) and every drop is recorded in `outcome.overrides`. There
is no last-write-wins mode: a dropped expectation that leaves no trace is exactly
the fail-open this module refuses.

## Common mistakes

| Mistake | Why it hurts | Do instead |
| --- | --- | --- |
| Treating `Indeterminate` as `Pass` | Certifies an unchecked result | Handle all three values |
| Returning `Found` with empty facts | Claims "I looked and saw nothing there" | Return `Missing` |
| Declaring `Sampled` without a frame | A sample without `sample_size`/`population` is refused | Include both numbers |
| Filtering a spec without an emptiness check | Empty spec = `NO_CHECKS`/`Indeterminate` | Assert `!checks.empty()` |
| Ignoring `budgetUsage.exceeded` | A truncated run reads as a complete one | Check it, or raise the cap |
| Inventing a fact key | The checker will not find the fact | Use the `kFactKeys` vocabulary |
| Assuming cross-output tolerance defaults to 0 | Unstated tolerance ≠ "exactly equal" | Declare the tolerance |

## Reference

- Module overview: [`README.md`](README.md)
- End-to-end teaching example: [`TEACHING_SCENARIO.md`](TEACHING_SCENARIO.md)
- Decision record: [`../adr/0172-unified-scientific-verifier.md`](../adr/0172-unified-scientific-verifier.md)
