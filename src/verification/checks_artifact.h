/***************************************************************************
  checks_artifact.h — CheckKind::ArtifactShape (RS14-10, Slice B)

  One check family for everything the artifact "is": that it exists, that it is
  the right kind of thing, that its grid is the declared one, that it carries
  the facts the plan relies on, and that its spatial frame matches. They belong
  together because a single `describe()` call answers all of them and because
  splitting them would let one outcome be reported without the others.

  The ordering below is fixed and part of the contract — when several things are
  wrong the SAME one is always reported, so two runs (and two reports) agree:

      1. spec is usable            (VERIFY.SPEC_INVALID)
      2. provider answered         (EVIDENCE_UNAVAILABLE / EVIDENCE_REFUSED)
      3. sampled record has a frame(EVIDENCE_UNAVAILABLE, never a fact)
      4. required keys present     (ARTIFACT_SCHEMA_MISMATCH)
      5. kind matches              (ARTIFACT_KIND_MISMATCH)
      6. grid matches              (ARTIFACT_GRID_MISMATCH)
      7. spatial frame matches     (ARTIFACT_GRID_MISMATCH when contradictory,
                                    EVIDENCE_UNAVAILABLE when simply absent)

  Step 7 is the deliberate repair: an artifact that carries NO CRS cannot be
  convicted of having the WRONG CRS. Those two used to collapse into
  CRS_MISMATCH in this repo, which redid five minutes of work to "fix" an IO
  hiccup. Here they differ in both status and failure code, so they are
  distinguishable on the wire by construction, not by reading the message.

  params:

    expect.kind           string   e.g. "raster" | "vector"
    expect.size           object   { "width": <n>, "height": <n> }
    expect.crs_authid     string   e.g. "EPSG:32648"
    required_keys         array    fact keys that must be present, each one a
                                   member of the mirrored kFactKeys vocabulary
    sampling              object   { "sample_size": <n>, "population": <n> }
                                   declares that what follows is an ESTIMATE

  At least one of expect.* / required_keys must be declared. `sampling` with
  either member missing is not repairable here: the evidence is incomplete and
  the check degrades to Indeterminate rather than promoting an estimate to a
  fact (see evidence.h).
 ***************************************************************************/
#pragma once

#include "verification/providers.h"
#include "verification/verification_types.h"

#include <string>
#include <vector>

namespace sicnu::verification
{

CheckResult runArtifactShapeCheck( const VerificationCheck &check, const VerificationInputs &inputs );

/// The closed `kFactKeys` vocabulary, MIRRORED rather than linked: the
/// authoritative table lives in an anonymous namespace in
/// src/agent/harness/workflow_ir.cpp and therefore is not linkable. Exported
/// precisely so a drift-guard test can later diff this against that table.
std::vector<std::string> mirroredFactKeys();

/// True when @p key is part of the mirrored fact vocabulary.
bool isMirroredFactKey( const std::string &key );

} // namespace sicnu::verification
