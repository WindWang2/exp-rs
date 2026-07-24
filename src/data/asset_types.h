#pragma once

#include <optional>

#include <QFlags>
#include <QString>
#include <QUuid>
#include <QtTypes>

namespace sicnu::data
{

class AssetId
{
  public:
    AssetId() = default;

    static AssetId generate();
    static std::optional<AssetId> fromString( const QString &text );

    bool isNull() const;
    QString toString() const;

    friend bool operator==( const AssetId &, const AssetId & ) = default;

  private:
    explicit AssetId( QUuid value );

    QUuid m_value;
};

class AssetRevision
{
  public:
    AssetRevision() = default;

    static constexpr AssetRevision initial()
    {
      return AssetRevision( 1 );
    }

    constexpr bool isValid() const
    {
      return m_value != 0;
    }

    constexpr quint64 value() const
    {
      return m_value;
    }

    friend bool operator==( const AssetRevision &, const AssetRevision & ) = default;

  private:
    explicit constexpr AssetRevision( quint64 value )
      : m_value( value )
    {
    }

    quint64 m_value = 0;
};

enum class AssetState
{
  Registered,
  Resolving,
  Ready,
  Missing,
  Offline,
  AuthenticationRequired,
  Error,
  Stale,
};

enum class AssetKind
{
  Raster,
  Vector,
  RemoteMap,
  VirtualRaster,
};

enum class AssetCapability : quint32
{
  None = 0,
  Renderable = 1U << 0,
  ReadablePixels = 1U << 1,
  BandMetadata = 1U << 2,
  BandStatistics = 1U << 3,
  QueryableFeatures = 1U << 4,
  EditableFeatures = 1U << 5,
  Temporal = 1U << 6,
  OfflineCacheable = 1U << 7,
  Exportable = 1U << 8,
  Relocatable = 1U << 9,
  DeletableSource = 1U << 10,
};
Q_DECLARE_FLAGS( AssetCapabilities, AssetCapability )

enum class PersistencePolicy
{
  ProjectPersistent,
  SessionTemporary,
  TaskTemporary,
};

enum class StorageKind
{
  File,
  TemporaryFile,
  Memory,
  Remote,
};

enum class LeaseKind
{
  View,
  Task,
  Edit,
};

enum class LeaseOutcome
{
  Released,
  Invalid,
};

} // namespace sicnu::data

Q_DECLARE_OPERATORS_FOR_FLAGS( sicnu::data::AssetCapabilities )
