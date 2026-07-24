#pragma once

#include <optional>
#include <utility>

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
                   QString displayName )
      : m_id( std::move( id ) )
      , m_revision( revision )
      , m_source( std::move( source ) )
      , m_kind( kind )
      , m_state( state )
      , m_capabilities( capabilities )
      , m_persistence( persistence )
      , m_storageKind( storageKind )
      , m_displayName( std::move( displayName ) )
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

struct UnloadPlan
{
  AssetId assetId;
  AssetRevision revision;
  quint64 catalogGeneration = 0;
  bool cascade = false;
  QVector<LeaseImpact> activeLeases;

  bool canUnload() const
  {
    if ( cascade )
      return true;
    return activeLeases.isEmpty();
  }
};

} // namespace sicnu::data
