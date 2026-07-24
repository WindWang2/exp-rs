#pragma once

#include <array>
#include <optional>
#include <utility>
#include <variant>

#include <QString>
#include <QVector>

#include "asset_types.h"
#include "data_result.h"
#include "source_descriptor.h"

namespace sicnu::data
{

class DataManager;

struct RegisterRequest
{
  SourceDescriptor source;
  PersistencePolicy persistence = PersistencePolicy::ProjectPersistent;
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

struct SpatialExtent
{
  double minimumX = 0.0;
  double minimumY = 0.0;
  double maximumX = 0.0;
  double maximumY = 0.0;
  bool valid = false;
};

struct RasterBandStructure
{
  int number = 0;
  QString dataType;
  std::optional<double> noDataValue;
  QString colorInterpretation;
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
};

struct VectorLayerStructure
{
  QString name;
  qint64 featureCount = -1;
  QString geometryType;
  QString crsWkt;
  SpatialExtent extent;
};

struct VectorStructure
{
  QString driverName;
  int layerCount = 0;
  QVector<VectorLayerStructure> layers;
};

using AssetStructure =
  std::variant<std::monostate, RasterStructure, VectorStructure>;

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

} // namespace sicnu::data
