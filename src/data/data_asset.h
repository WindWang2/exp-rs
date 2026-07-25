#pragma once

#include <array>
#include <optional>
#include <utility>
#include <variant>

#include <QString>
#include <QVector>

#include "asset_types.h"
#include "collection_types.h"
#include "data_result.h"
#include "source_descriptor.h"

namespace sicnu::data
{

class DataManager;

struct RegisterRequest
{
  SourceDescriptor source;
  PersistencePolicy persistence = PersistencePolicy::ProjectPersistent;
  /// Capabilities the registrar asserts in addition to those the source
  /// provider inferred. Used by publishers (e.g. the OutputCommitter) to
  /// declare `DeletableSource` on an asset whose file the Data Manager owns
  /// and may legitimately delete on reap.
  AssetCapabilities additionalCapabilities;
};

struct RegisterResult
{
  AssetId assetId;
  bool reusedExisting = false;
  QVector<Diagnostic> diagnostics;
};

struct RestoreRequest
{
  AssetId id;
  AssetRevision revision = AssetRevision::initial();
  SourceDescriptor source;
  PersistencePolicy persistence = PersistencePolicy::ProjectPersistent;
};

struct RelocateRequest
{
  AssetId id;
  SourceDescriptor replacement;
};

struct RelocateResult
{
  AssetId assetId;
  AssetRevision revision;
  QVector<Diagnostic> diagnostics;
};

struct SpatialExtent
{
  double minimumX = 0.0;
  double minimumY = 0.0;
  double maximumX = 0.0;
  double maximumY = 0.0;
  bool valid = false;

  friend bool operator==( const SpatialExtent &, const SpatialExtent & ) = default;
};

struct RasterBandStructure
{
  int number = 0;
  QString dataType;
  std::optional<double> noDataValue;
  QString colorInterpretation;

  friend bool operator==( const RasterBandStructure &, const RasterBandStructure & ) = default;
};

struct RasterStructure
{
  QString driverName;
  int width = 0;
  int height = 0;
  int bandCount = 0;
  QString crsWkt;
  bool hasGeoTransform = false;
  std::array<double, 6> geoTransform{};
  SpatialExtent extent;
  QVector<RasterBandStructure> bands;

  friend bool operator==( const RasterStructure &, const RasterStructure & ) = default;
};

struct VectorLayerStructure
{
  QString name;
  qint64 featureCount = -1;
  QString geometryType;
  QString crsWkt;
  SpatialExtent extent;

  friend bool operator==( const VectorLayerStructure &, const VectorLayerStructure & ) = default;
};

struct VectorStructure
{
  QString driverName;
  int layerCount = 0;
  QVector<VectorLayerStructure> layers;

  friend bool operator==( const VectorStructure &, const VectorStructure & ) = default;
};

using AssetStructure =
  std::variant<std::monostate, RasterStructure, VectorStructure>;

/// True when a replacement source is structurally compatible with the current
/// asset — same kind and the same essential shape (raster: driver, dimensions,
/// band count; vector: layer count and per-layer shape). Relocation validates
/// this before mutating the catalog, so a moved source cannot silently swap in
/// an incompatible dataset under an existing Asset ID.
inline bool structuresCompatible( const AssetStructure &current,
                                  const AssetStructure &replacement )
{
  // A monostate current structure carries no shape to validate against — this
  // happens when an asset's source is missing or unresolved. Recovery
  // relocation to a resolvable source is allowed; the new structure is adopted.
  if ( std::holds_alternative<std::monostate>( current ) )
    return true;

  // A current structure cannot be silently downgraded to an unknown one: a
  // replacement that resolves to no structure is not a compatible move.
  if ( std::holds_alternative<std::monostate>( replacement ) )
    return false;

  if ( current.index() != replacement.index() )
    return false;

  if ( const auto *currentRaster = std::get_if<RasterStructure>( &current ) )
  {
    const auto *replacementRaster = std::get_if<RasterStructure>( &replacement );
    return replacementRaster &&
           currentRaster->driverName == replacementRaster->driverName &&
           currentRaster->width == replacementRaster->width &&
           currentRaster->height == replacementRaster->height &&
           currentRaster->bandCount == replacementRaster->bandCount;
  }

  if ( const auto *currentVector = std::get_if<VectorStructure>( &current ) )
  {
    const auto *replacementVector = std::get_if<VectorStructure>( &replacement );
    return replacementVector &&
           currentVector->driverName == replacementVector->driverName &&
           currentVector->layerCount == replacementVector->layerCount &&
           currentVector->layers == replacementVector->layers;
  }

  return true;
}

class AssetSnapshot
{
  public:
    const AssetId &id() const
    {
      return m_id;
    }

    AssetRevision revision() const
    {
      return m_revision;
    }

    const SourceDescriptor &source() const
    {
      return m_source;
    }

    AssetKind kind() const
    {
      return m_kind;
    }

    AssetState state() const
    {
      return m_state;
    }

    AssetCapabilities capabilities() const
    {
      return m_capabilities;
    }

    PersistencePolicy persistence() const
    {
      return m_persistence;
    }

    StorageKind storageKind() const
    {
      return m_storageKind;
    }

    const QString &displayName() const
    {
      return m_displayName;
    }

    const AssetStructure &structure() const
    {
      return m_structure;
    }

    /// The parent Data Collection this asset belongs to, if any. A standalone
    /// asset has no parent; a child of a collection carries its CollectionId.
    std::optional<CollectionId> parentCollectionId() const
    {
      return m_parentCollectionId;
    }

  private:
    friend class DataManager;

    AssetSnapshot( AssetId id,
                   AssetRevision revision,
                   SourceDescriptor source,
                   AssetKind kind,
                   AssetState state,
                   AssetCapabilities capabilities,
                   PersistencePolicy persistence,
                   StorageKind storageKind,
                   QString displayName,
                   AssetStructure structure )
      : m_id( std::move( id ) )
      , m_revision( revision )
      , m_source( std::move( source ) )
      , m_kind( kind )
      , m_state( state )
      , m_capabilities( capabilities )
      , m_persistence( persistence )
      , m_storageKind( storageKind )
      , m_displayName( std::move( displayName ) )
      , m_structure( std::move( structure ) )
    {
    }

    AssetId m_id;
    AssetRevision m_revision;
    SourceDescriptor m_source;
    AssetKind m_kind;
    AssetState m_state;
    AssetCapabilities m_capabilities;
    PersistencePolicy m_persistence;
    StorageKind m_storageKind;
    QString m_displayName;
    AssetStructure m_structure;
    std::optional<CollectionId> m_parentCollectionId;
};

struct AssetQuery
{
  std::optional<AssetKind> kind;
  std::optional<AssetState> state;
  std::optional<PersistencePolicy> persistence;
};

struct AssetRef
{
  AssetId id;
  AssetRevision expectedRevision;
};

struct AssetUse
{
  LeaseKind kind = LeaseKind::View;
  QString purpose;
};

struct LeaseRef
{
  AssetId assetId;
  quint64 token = 0;
  LeaseKind kind = LeaseKind::View;

  friend bool operator==( const LeaseRef &, const LeaseRef & ) = default;
};

struct LeaseImpact
{
  LeaseRef lease;
  QString purpose;
};

class UnloadPlan
{
  public:
    const AssetId &assetId() const
    {
      return m_assetId;
    }

    AssetRevision revision() const
    {
      return m_revision;
    }

    quint64 catalogGeneration() const
    {
      return m_catalogGeneration;
    }

    bool cascade() const
    {
      return m_cascade;
    }

    const QVector<LeaseImpact> &activeLeases() const
    {
      return m_activeLeases;
    }

    bool canUnload() const
    {
      return m_cascade || m_activeLeases.isEmpty();
    }

    UnloadPlan confirmedCascade() const
    {
      UnloadPlan confirmed = *this;
      confirmed.m_cascade = true;
      return confirmed;
    }

  private:
    friend class DataManager;

    UnloadPlan( AssetId assetId,
                AssetRevision revision,
                quint64 catalogGeneration,
                QVector<LeaseImpact> activeLeases )
      : m_assetId( assetId )
      , m_revision( revision )
      , m_catalogGeneration( catalogGeneration )
      , m_activeLeases( std::move( activeLeases ) )
    {
    }

  AssetId m_assetId;
  AssetRevision m_revision;
  quint64 m_catalogGeneration = 0;
  bool m_cascade = false;
  QVector<LeaseImpact> m_activeLeases;
};

/// Request to reap a temporary Data Asset: remove it from the catalog and,
/// when it declares `DeletableSource`, delete its on-disk source file. Reaping
/// is the capability-limited deletion command distinct from unload (unload
/// never deletes source data). Only `SessionTemporary` / `TaskTemporary`
/// assets may be reaped; an asset holding an active lease is refused.
struct ReapRequest
{
  AssetId id;
};

/// Outcome of a reap. `unloaded` is true when the asset was removed from the
/// catalog; `sourceDeleted` is true only when the on-disk file was deleted
/// (false when the asset was not `DeletableSource`, or when deletion failed).
/// A failed deletion is reported as a Warning diagnostic alongside a true
/// `unloaded` - the catalog never points at a deleted file, and disk orphans
/// are surfaced rather than hidden.
struct ReapResult
{
  bool unloaded = false;
  bool sourceDeleted = false;
  QVector<Diagnostic> diagnostics;
};

/// Outcome of a temporary-asset sweep (session-scope or task-scope) that
/// reaps every idle temporary asset of one policy. Leased temporaries of that
/// policy are skipped (not force-revoked) and their ids reported so the host
/// can decide what to do. Assets of the other policies are never touched.
struct TemporaryReapResult
{
  /// Number of temporary assets reaped (removed from the catalog).
  int reapedCount = 0;
  /// Temporary assets that held an active lease and were left in place.
  QVector<AssetId> skippedLeased;
  /// Per-asset diagnostics from the sweep (e.g. file-deletion warnings).
  QVector<Diagnostic> diagnostics;
};

} // namespace sicnu::data
