// src/processing/algorithms/temporal/temporal_workspace.h
// TemporalCollection ⇆ DataManager / Workspace binding.
//
// This is the seam that makes a TemporalCollection a first-class workspace
// asset instead of a path-list JSON file:
//
//   - createCollection / updateCollection / loadCollection persist the typed
//     TemporalCollection into the DataManager's temporal-collection records
//     (project-persistent via the `<temporalCollections>` serializer block).
//   - bindCollectionAssets resolves every scene path against the DataManager
//     and stores the scene's (assetId, revision) on the TemporalSceneRef, so
//     scene identity is the ASSET, with the path as fallback/diagnostics.
//   - collectionFingerprintInputs produces revision-aware fingerprint inputs:
//     one entry for the collection record (id @ record revision) and one per
//     scene (assetId @ CURRENT asset revision — resolved live, never a stale
//     stored snapshot, so a scene re-commit invalidates every cached step
//     that consumed the collection). A collection with ANY unbound scene
//     yields no fingerprint inputs: a path-only scene has no data identity,
//     and a cache that cannot prove revision validity must not claim a hit.
#pragma once

#include "temporal_collection.h"

#include "data/collection_types.h"
#include "data/data_result.h"
#include "data/execution_fingerprint.h"

#include <QString>

#include <json/json.h>

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::temporal
{

// --- Process-wide workspace catalog ---------------------------------------
// The host (GUI / MCP / CLI) sets its DataManager once at startup so the
// temporal operators can resolve a `collection` parameter that addresses a
// workspace record by id. Not owning; the pointer must outlive processing
// (the host destroys it last, like the TaskCenter/OutputCommitter seams).
void setWorkspaceCatalog( sicnu::data::DataManager *catalog );
sicnu::data::DataManager *workspaceCatalog();

/// Serializes the collection into the canonical descriptor document stored by
/// the DataManager record (readable by TemporalCollection::fromJson).
QString collectionDescriptorText( const TemporalCollection &collection );

/// Parses a stored descriptor document. Returns false + @a error on invalid
/// schema/JSON.
bool collectionFromDescriptorText( const QString &descriptor, TemporalCollection *out,
                                   QString *error );

/// Resolves every scene path against @a dataManager and stores the scene's
/// (assetId, revision) on the ref (path stays as fallback/diagnostics).
/// Returns the number of scenes bound to a registered asset.
int bindCollectionAssets( TemporalCollection &collection, sicnu::data::DataManager *dataManager );

/// Registers (or updates) a temporal collection record in the workspace:
/// binds scene assets, serializes the descriptor, and creates a new record
/// (deduped by name + descriptor) or updates the existing one addressed by
/// @a existingId. Returns the record id; empty on failure (see @a error).
/// @a reusedExisting reports the dedup outcome when non-null.
sicnu::data::CollectionId saveCollectionToWorkspace( sicnu::data::DataManager &dataManager,
                                                     const QString &displayName,
                                                     const TemporalCollection &collection,
                                                     const sicnu::data::CollectionId &existingId
                                                       = sicnu::data::CollectionId(),
                                                     QString *error = nullptr,
                                                     bool *reusedExisting = nullptr );

/// Loads the typed collection stored in a workspace record. Returns false +
/// @a error when the record is unknown or the descriptor does not parse.
bool loadCollectionFromWorkspace( sicnu::data::DataManager &dataManager, const sicnu::data::CollectionId &id,
                                  TemporalCollection *out, QString *error = nullptr );

/// Fingerprint inputs for a workflow/operator step consuming @a collection:
/// one TaggedDerivationInput for the collection record (id @ record revision)
/// plus one per scene (assetId @ CURRENT asset revision, resolved live from
/// the DataManager). Appends to @a out. Returns false when the collection is
/// not workspace-bound or any scene is unbound (path-only identity) — the
/// caller must not cache such a step, because no fingerprint can prove that
/// a cached result still matches the scene data.
bool collectionFingerprintInputs( sicnu::data::DataManager &dataManager, const sicnu::data::CollectionId &id,
                                  const TemporalCollection &collection,
                                  QVector<sicnu::data::TaggedDerivationInput> *out );

/// Resolves the collection addressed by an operator's `collection` parameter
/// and produces the fingerprint inputs for it. Accepts a workspace collection
/// id (UUID string) or a descriptor file path whose collection is registered
/// in the workspace. Returns false (with @a reason) when the parameter does
/// not address a workspace-bound collection — caching then falls back to the
/// conservative "not cacheable" verdict; execution is unaffected.
bool fingerprintInputsForCollectionParam( sicnu::data::DataManager *dataManager, const QString &collectionParam,
                                          QVector<sicnu::data::TaggedDerivationInput> *out,
                                          QString *reason = nullptr );

/// Complete input-identity policy for an operator task's parameters: resolves
/// every generic path parameter, inline scene entry ("scenes" strings and
/// {path,...} objects) and the `collection` parameter into
/// TaggedDerivationInputs. Returns false (with @a reason) when ANY input
/// cannot be revision-identified — the conservative uncacheable verdict that
/// keeps the execution cache honest (a cache that cannot prove input
/// identity must never serve a hit). Appends to @a out on success.
///
/// Identity classes (#726): a value classified by QgsDataSourceResolver as a
/// plain local file must exist AND resolve to a registered asset; a remote /
/// VSI / OGR-connection datasource (/vsicurl/, /vsis3/, https://, PG:, …)
/// must resolve to a registered asset through its registered canonical
/// source. ANY unresolvable datasource fails the whole step — remote inputs
/// are never silently omitted the way a QFileInfo existence gate used to.
/// Parameter keys in the platform output vocabulary ("output"/"result"-like)
/// are destinations, not inputs, and are skipped by KEY — the destination
/// value under any other key stays an input (an in-place run
/// {input:x, output:x} fingerprints with x's revision). In-pipeline producer
/// outputs are not visible here: their PARAMETER KEYS are passed via
/// @a chainedProducerKeys so the datasource scan never double-keys them by
/// file revision — an intermediate's real identity is the producer's
/// fingerprint, not its registration revision. Excluding by KEY (not by
/// value) keeps a literal parameter that merely carries a producer path
/// inside the scanned identity. A string that LOOKS like an unclassified
/// GDAL datasource (identifier-prefix + colon, e.g. HDF5:/netcdf: subdataset
/// syntax) must resolve to a registered asset like any remote input — it is
/// never treated as a plain scientific parameter.
bool fingerprintInputsForOperatorParams( sicnu::data::DataManager *dataManager,
                                         const QVariantMap &params,
                                         QVector<sicnu::data::TaggedDerivationInput> *out,
                                         QString *reason = nullptr,
                                         const QStringList &chainedProducerKeys
                                           = QStringList() );

} // namespace sicnu::temporal
