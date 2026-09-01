#pragma once

#include <array>
#include <optional>
#include <utility>
#include <variant>

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

// QJsonObject/QJsonArray are used only by RemoteMapStructure's diagnostics-only
// toJson/fromJson, defined in data_asset.cpp; the header forwards them via
// incomplete types in the declarations.
class QJsonObject;

#include "asset_types.h"
#include "band_role.h"
#include "collection_types.h"
#include "data_result.h"
#include "source_descriptor.h"
#include "virtual_raster_recipe.h"

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
  /// Optional acquisition time the registrar asserts (e.g. a STAC item
  /// datetime an importer carries from its preview). Empty by default; when
  /// engaged it lands on the resulting DataAsset's acquisition-time field.
  std::optional<QDateTime> acquisitionTime;
  /// Re-commit over an already-registered stable path (#687): when the dedup
  /// by SourceKey hits and this flag is set, the caller asserts it replaced
  /// the bytes at that path (the OutputCommitter publish-then-swap does).
  /// The existing asset is treated as updated: its structure snapshot is
  /// refreshed from the fresh resolution, its revision advances one step
  /// (mirroring relocate), and one `assetChanged` is emitted so displayed
  /// layers reload. Without the flag a dedup hit still bumps when the fresh
  /// structure differs from the snapshot (an externally mutated source), and
  /// otherwise reuses the asset unchanged.
  bool notifyUpdateOnReuse = false;
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
  /// Optional acquisition time restored from a persisted project. Empty for a
  /// freshly registered asset (no source implies one) and for legacy restores
  /// that pre-date this field; populated by the project serializer on reload.
  std::optional<QDateTime> acquisitionTime;
};

/// Restore a Virtual Raster Asset from a persisted recipe. The recipe - not any
/// scratch `.vrt` path - is the identity: the artifact is regenerated on
/// resolve. An input that was not restored is tolerated: the virtual asset is
/// kept in the catalog in a non-Ready state and its dependency edge is skipped
/// with a Warning (missing dependencies do not drop the asset).
struct RestoreVirtualRasterRequest
{
  AssetId id;
  VirtualRasterRecipe recipe;
  AssetRevision revision = AssetRevision::initial();
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
  /// Semantic role assigned by product discovery (stacked products carry it in
  /// the `SICNU_BAND_ROLE` band metadata). Unknown for plain rasters.
  BandRole role = BandRole::Unknown;

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

/// Structural metadata for a Remote Map Asset (WMS/WMTS/TMS/XYZ). It carries
/// only what a web-map service can honestly report: the declared layer set,
/// advertised CRS list, service-reported extent, image format, and (for tiled
/// services) tile-matrix resolution + z-range. It deliberately does NOT model
/// bands, data types, or statistics — a remote map is renderable but not a
/// pixel-analysis source (parent spec line 109).
///
/// The toJson()/fromJson() round-trip is a deliberate new affordance (the
/// raster/vector structures are plain aggregates without JSON); it is used for
/// diagnostics/debugging only — the persisted identity of a remote map is its
/// SourceDescriptor, and the structure is re-derived by re-probing on restore.
struct RemoteMapStructure
{
  RemoteMapService service = RemoteMapService::Wms;
  QStringList layerNames;
  QStringList crsList;
  SpatialExtent extent;
  QString imageFormat;
  std::optional<double> pixelSizeX;
  std::optional<double> pixelSizeY;
  int zMin = 0;
  int zMax = 0;
  bool valid = false;

  friend bool operator==( const RemoteMapStructure &,
                          const RemoteMapStructure & ) = default;

  // Definitions live in data_asset.cpp (keeps the QJsonObject assembly out of
  // this widely-included header). Diagnostics-only serialization: the persisted
  // identity of a remote map is its SourceDescriptor, not this structure.
  QJsonObject toJson() const;
  static Result<RemoteMapStructure> fromJson( const QJsonObject &json );
  static QString serviceToString( RemoteMapService kind );
  static std::optional<RemoteMapService> serviceFromString( const QString &name );
};

using AssetStructure =
  std::variant<std::monostate, RasterStructure, VectorStructure, RemoteMapStructure>;

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

  // A remote map relocates compatibly when the service family and the declared
  // layer set match (the identity-relevant shape). Content drift a service may
  // publish over time (a newly advertised CRS, a tweaked extent) does NOT block
  // the move — relocate surfaces it via assetChanged rather than refusing.
  if ( const auto *currentRemote = std::get_if<RemoteMapStructure>( &current ) )
  {
    const auto *replacementRemote =
      std::get_if<RemoteMapStructure>( &replacement );
    return replacementRemote &&
           currentRemote->service == replacementRemote->service &&
           currentRemote->layerNames == replacementRemote->layerNames;
  }

  // Every variant arm is handled above; reaching here means a future arm was
  // added without a compatibility branch. Fail loudly rather than silently
  // approving an incompatible relocation.
  Q_UNREACHABLE();
  return false;
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

    /// When this asset's source was acquired, if known. Empty for assets whose
    /// source carries no acquisition time (the default); populated by restores
    /// that persisted it (e.g. STAC item datetime in a later ticket). Returned
    /// by value to match parentCollectionId(): AssetSnapshot is a value type and
    /// QDateTime is implicitly shared, so the copy is a cheap refcount bump and
    /// callers cannot dangle a reference into a temporary snapshot.
    std::optional<QDateTime> acquisitionTime() const
    {
      return m_acquisitionTime;
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
                   AssetStructure structure,
                   std::optional<QDateTime> acquisitionTime = {},
                   std::optional<CollectionId> parentCollectionId = {} )
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
      , m_parentCollectionId( std::move( parentCollectionId ) )
      , m_acquisitionTime( std::move( acquisitionTime ) )
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
    std::optional<QDateTime> m_acquisitionTime;
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

    /// Assets with a strong dependency on this asset (its consumers), in
    /// edge-insertion order. Normal unload is refused while this list is
    /// non-empty; a confirmed cascade removes them transitively, deepest-first.
    const QVector<AssetId> &strongDependents() const
    {
      return m_strongDependents;
    }

    bool canUnload() const
    {
      return m_cascade || ( m_activeLeases.isEmpty() && m_strongDependents.isEmpty() );
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
                QVector<LeaseImpact> activeLeases,
                QVector<AssetId> strongDependents = {} )
      : m_assetId( assetId )
      , m_revision( revision )
      , m_catalogGeneration( catalogGeneration )
      , m_activeLeases( std::move( activeLeases ) )
      , m_strongDependents( std::move( strongDependents ) )
    {
    }

  AssetId m_assetId;
  AssetRevision m_revision;
  quint64 m_catalogGeneration = 0;
  bool m_cascade = false;
  QVector<LeaseImpact> m_activeLeases;
  QVector<AssetId> m_strongDependents;
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
  /// Temporary assets left in place because they were not idle: they held an
  /// active lease or were consumed by a strong dependent (reaping either
  /// would be refused).
  QVector<AssetId> skippedLeased;
  /// Per-asset diagnostics from the sweep (e.g. file-deletion warnings).
  QVector<Diagnostic> diagnostics;
};

} // namespace sicnu::data
