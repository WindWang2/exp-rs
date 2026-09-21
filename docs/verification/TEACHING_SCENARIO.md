# Teaching scenario: why a result cannot be judged

This is a worked end-to-end example of the verifier used for its original
purpose — explaining to a student, honestly, what is and is not known about a
result. It walks one computation through all three verdicts.

## The setting

A student computes an NDVI product from a two-band scene and asks whether the
result is trustworthy. They have the result file, and they have three different
levels of evidence available depending on what they did before saving it.

## What is declared

The NDVI pack is derived from the scientific-contract registry rather than typed
by hand, so the expectations move with the contract:

```cpp
DerivedPack derived = scientificContractFor( "ndvi" );
VerifierPack structural = structuralPack();
const ComposeOutcome spec = composePacks( { structural, derived.pack },
                                          PackConflictPolicy::Reject );
```

Four checks come out of the projection, each carrying the contract's own
literals:

| Check id | Family | Says |
| --- | --- | --- |
| `contract.domain_transition` | State invariant | reflectance in → index out |
| `contract.scale_offset` | State invariant | the declared scale/offset mapping |
| `contract.no_data` | Numeric range | invalid samples handled by the declared policy |
| `contract.provenance` | Provenance | provenance recorded at the declared level |

Note what is *not* here: the NDVI closed form, the band mapping, the range. Those
live in the registry. If the contract is corrected next term, these checks move
with it — which is the point of projecting rather than copying.

---

## Scenario A — the evidence is there and it holds

The student ran the pipeline with the product and the provenance sidecar both
written.

```cpp
VerificationInputs inputs;
inputs.artifact   = &productProvider;   // reads the saved raster
inputs.metric     = &metricProvider;    // reads the computed NDVI statistics
inputs.provenance = &provenanceProvider;// reads the sidecar
inputs.sourceId   = "student-run-42";
```

```
report.status              = Pass
teaching.verdict           = "trustworthy"
teaching.headline          = "This result can be trusted: every declared
                              expectation was met."
```

**What the student learns:** the result is trustworthy *for the expectations that
were declared*. Not "correct" in some absolute sense — correct with respect to
the contract. That distinction is itself a lesson, and the wording preserves it.

---

## Scenario B — the evidence is there and it contradicts the declaration

The student computed NDVI but wrote the product back through the display
stretch, so the stored values are byte-scaled rather than reflectances.

```
report.status              = Fail
report.outcome.failureCodes = { VERIFY.STATE_INVARIANT_VIOLATION }

teaching.verdict           = "not_trustworthy"
teaching.headline          = "This result is not trustworthy: 1 declared
                              expectation(s) were violated."
teaching.reasons[0]        = "the operator consumes a 'reflectance' surface and
                              produces a 'reflectance' surface — the recorded
                              input domain is 'dn'"
```

**What the student learns:** a specific, actionable fact. Not "the result is
bad", but "the surface you fed in was DN, and the contract says reflectance".
The `howToFix` field on that check names the conversion. This is the case the
renderer is easiest to get right, and the least interesting one.

---

## Scenario C — the evidence is not there

The student saved the raster but closed the session before the provenance
sidecar was written, and the metrics were computed in a notebook that is gone.

```cpp
class AbsentProvenance : public ProvenanceProvider
{
  public:
    Availability completeness( const std::string &, std::vector<ProvenanceDimension> &,
                               std::string &reason ) const override
    {
        reason = "no provenance sidecar was written for this run";
        return Availability::Missing;   // NOT an empty vector
    }
};
```

```
report.status              = Indeterminate
report.outcome.failureCodes = { VERIFY.EVIDENCE_UNAVAILABLE }

teaching.verdict           = "unverified"
teaching.headline          = "This result cannot be judged yet: 1 of the
                              declared expectations could not be concluded from
                              the evidence available."
teaching.reasons[0]        = "provenance.completeness could not be judged, so the
                              result stays undecided"
```

**What the student learns:** the result is **not wrong** and it is **not fine**.
Nobody checked the provenance, because there is no provenance to check. The
honest answer is that this question is open.

### Why this scenario is the reason the module exists

Under a two-valued scheme, Scenario C and Scenario A would produce the same
output. The provenance check would not fire — there is nothing to compare — and
"no check complained" would be reported as success. A student acting on that
would trust a result for which the provenance question had never been *asked*,
let alone answered.

The three-valued lattice makes that impossible:

```cpp
REQUIRE( combineStatus( CheckStatus::Pass, CheckStatus::Indeterminate )
         == CheckStatus::Indeterminate );   // never Pass
REQUIRE( combineAll( {} ) == CheckStatus::Indeterminate );  // no evidence is not success
```

And the teaching renderer keeps the vocabulary straight — a student is never
shown the word "trusted" for an unverified result:

```
REQUIRE( haystack.find( "trusted" ) == std::string::npos );  // for the unverified view
```

That assertion is in `test_verifier_render_14`, and it is one of the four that go
red if the `Indeterminate` headline is ever allowed to borrow the `Pass` wording
(mutation F-1).

---

## The teaching point

Three results, three verdicts, and the difference between them is *not* the
quality of the computation — the computation is the same in all three cases. The
difference is what could be established:

| | Scenario A | Scenario B | Scenario C |
| --- | --- | --- | --- |
| Computation | same | same | same |
| Evidence available | yes | yes | **no** |
| Expectation holds | yes | no | **unknown** |
| Verdict | `Pass` | `Fail` | `Indeterminate` |
| Student told | trusted | violated, here is how to fix it | cannot be judged yet |

The lesson the module is built to teach: **"I could not check" is a third
answer, and reporting it as either of the other two is a lie** — one of optimism,
the other of pessimism. A scientific pipeline needs all three.

## Trying it yourself

The scenario is exercised by:

- `tests/test_verifier_checks_14.cpp` — the B-6 case, which requires that "CRS
  absent" and "CRS contradictory" produce *different* verdicts (this is the case
  mutations B-2 and B-3 attack);
- `tests/test_verifier_render_14.cpp` — the rendering guarantees above, including
  the forbidden-wording guard;
- `tests/test_verifier_adversarial_14.cpp` — the battery that tries to make an
  evidence-free run report `Pass` and fails if it can.
