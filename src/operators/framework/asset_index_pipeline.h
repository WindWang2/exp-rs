/***************************************************************************
 * asset_index_pipeline.h  —  Asset-driven spectral-index execution
 ***************************************************************************
 * First real caller of the Processing Asset Resolver (#37) and the
 * transactional Output Committer (#39). Runs the rs:spectral_index operator
 * from a Data Asset input: resolve the AssetRef (rejecting a stale revision
 * before any work), hold a Task lease for the run, pass the resolved source
 * location to the operator body (unchanged), then commit the output through
 * the OutputCommitter — or discard the temporary output on failure.
 ***************************************************************************/
#pragma once

#include <QString>

#include "data/asset_types.h"
#include "data/data_result.h"
#include "processing/framework/output_committer.h"

namespace sicnu::data
{
class ProcessingAssetResolver;
struct AssetRef;
}

namespace sicnu::operators
{

/// Spectral-index parameters, mirroring the operator's parameter keys.
struct SpectralIndexParams
{
  QString index = QStringLiteral( "NDVI" );
  int nir = 0;
  int red = 0;
  int green = 0;
  int blue = 0;
  int swir = 0;
};

/// Where the algorithm writes (tempPath) and where the committer publishes
/// (stablePath), plus the persistence policy and opt-in display flag.
struct StableOutputSpec
{
  QString tempPath;
  QString stablePath;
  bool autoLoad = false;
  sicnu::data::PersistencePolicy persistence = sicnu::data::PersistencePolicy::SessionTemporary;
};

/// Runs rs:spectral_index from a Data Asset input.
///
/// Flow: resolve the `input` AssetRef via `resolver` (this acquires a
/// LeaseKind::Task lease held for the whole run; a stale expectedRevision is
/// rejected with a clear diagnostic before any work); build the operator
/// parameter map from `params` and the resolved source location; run the
/// rs:spectral_index operator (body unchanged); on success commit the output
/// through `committer` with a Derivation Record carrying the algorithm id,
/// parameter snapshot, input AssetId+revision, and execution info; on failure
/// discard the temporary output. Returns the commit result (the registered
/// output AssetId) or structured diagnostics.
sicnu::CommitResult runSpectralIndexFromAsset(
  const sicnu::data::AssetRef &input,
  const SpectralIndexParams &params,
  const StableOutputSpec &output,
  const sicnu::data::ProcessingAssetResolver &resolver,
  sicnu::OutputCommitter &committer );

} // namespace sicnu::operators
