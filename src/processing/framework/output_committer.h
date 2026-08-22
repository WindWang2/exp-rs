#pragma once

#include <QObject>
#include <QString>

#include "data/asset_types.h"
#include "data/data_result.h"
#include "data/derivation_record.h"

namespace sicnu::data
{
class DataManager;
}

namespace sicnu
{

class TaskCenter;

} // namespace sicnu

namespace sicnu
{

/// Request describing one algorithm output to commit transactionally.
struct AlgorithmOutputRequest
{
  /// Kind of output. Selects the validation provider (raster vs vector).
  sicnu::data::AssetKind kind = sicnu::data::AssetKind::Raster;
  /// Path the algorithm wrote its result to. Consumed (moved) on success.
  QString tempPath;
  /// Final, stable path the output is published to. Must be on the same
  /// filesystem as `tempPath` for an atomic rename.
  QString stablePath;
  /// Persistence policy for the registered Data Asset. Defaults to
  /// `SessionTemporary` — algorithm outputs survive the session but are not
  /// written into the `.qgz` project until explicitly promoted.
  sicnu::data::PersistencePolicy persistence = sicnu::data::PersistencePolicy::SessionTemporary;
  /// When true, the committer emits `displayRequested` once the asset is
  /// registered. Defaults to false: display is opt-in, never automatic.
  bool autoLoad = false;
  /// Provenance attached to the registered asset. `outputAssetId` is stamped
  /// by the committer with the freshly-registered Asset ID.
  sicnu::data::DerivationRecord derivation;
};

using CommitResult = sicnu::data::Result<sicnu::data::AssetId>;

/// Transactional commit for algorithm outputs, performed as a distinct step
/// after an algorithm reports success.
///
/// Steps on success: validate the temp output exists and is structurally
/// openable for its kind; atomically publish it to the stable path; register
/// the stable output as a Data Asset; attach the Derivation Record. `assetAdded`
/// fires once (from the Data Manager) and, if `autoLoad` was requested, the
/// committer emits `displayRequested` exactly once.
///
/// On any failure (missing/unopenable temp, failed publish, failed
/// registration) nothing is registered, nothing is published, and the temp
/// output is left in place for diagnosis. A failed or cancelled task should
/// call `discardTemporary` instead, which removes the temp output and
/// registers nothing — the catalog never holds an apparently-valid output from
/// an incomplete task.
#include "sicnu_processing_export.h"

class SICNU_PROCESSING_EXPORT OutputCommitter : public QObject
{
  Q_OBJECT

  public:
    explicit OutputCommitter( sicnu::data::DataManager *dataManager, QObject *parent = nullptr );

    /// Commits `request` transactionally. On failure returns structured
    /// diagnostics and leaves the catalog and stable path untouched.
    CommitResult commit( const AlgorithmOutputRequest &request );

    /// Discards a temporary output from a failed or cancelled task. Removes
    /// `tempPath` if present. Registers nothing and publishes nothing.
    void discardTemporary( const QString &tempPath );

  signals:
    /// Emitted once when `autoLoad` was requested and the asset was
    /// successfully registered. The caller decides whether to add a Display
    /// Layer; display is never automatic.
    void displayRequested( sicnu::data::AssetId id );

  private:
    sicnu::data::DataManager *m_dataManager = nullptr; // not owned
};

} // namespace sicnu
