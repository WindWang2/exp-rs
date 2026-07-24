#pragma once

#include <optional>
#include <utility>

#include <QString>

#include "asset_types.h"
#include "data_asset.h"
#include "data_manager.h"
#include "data_result.h"
#include "source_descriptor.h"

namespace sicnu::data
{

/// Immutable algorithm-native description of a Data Asset, resolved once for a
/// processing request. It carries the resolved source location and declared
/// capabilities an algorithm needs, with no live handles, no `QgsMapLayer *`,
/// no `GDALDataset *`, and no credentials. Algorithms may consume the source
/// location (path/URI) directly; the Data Manager remains the identity
/// authority.
class ResolvedAssetSnapshot
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

    AssetKind kind() const
    {
      return m_kind;
    }

    AssetCapabilities capabilities() const
    {
      return m_capabilities;
    }

    /// Algorithm-native source location (path/URI) for the asset.
    const QString &sourceLocation() const
    {
      return m_source.canonicalSource;
    }

    /// Non-secret source description. Carries only an `authConfigId` reference,
    /// never credential material, consistent with the Data Asset descriptor rules.
    const SourceDescriptor &source() const
    {
      return m_source;
    }

  private:
    friend class ProcessingAssetResolver;

    ResolvedAssetSnapshot( AssetId id,
                           AssetRevision revision,
                           AssetKind kind,
                           AssetCapabilities capabilities,
                           SourceDescriptor source )
      : m_id( std::move( id ) )
      , m_revision( revision )
      , m_kind( kind )
      , m_capabilities( capabilities )
      , m_source( std::move( source ) )
    {
    }

    AssetId m_id;
    AssetRevision m_revision;
    AssetKind m_kind;
    AssetCapabilities m_capabilities;
    SourceDescriptor m_source;
};

/// Move-only handle binding an immutable algorithm input snapshot to the Task
/// lease that protects the underlying asset for the life of a processing run.
/// Releasing or destroying the handle releases the Task lease.
class ResolvedAsset
{
  public:
    ResolvedAsset( const ResolvedAsset & ) = delete;
    ResolvedAsset &operator=( const ResolvedAsset & ) = delete;
    ResolvedAsset( ResolvedAsset && ) noexcept = default;
    ResolvedAsset &operator=( ResolvedAsset && ) noexcept = default;
    ~ResolvedAsset() = default;

    bool isValid() const;

    const ResolvedAssetSnapshot &snapshot() const
    {
      return m_snapshot;
    }

  private:
    friend class ProcessingAssetResolver;

    explicit ResolvedAsset( ResolvedAssetSnapshot snapshot, AssetLease lease );

    ResolvedAssetSnapshot m_snapshot;
    std::optional<AssetLease> m_lease;
};

/// Single seam that turns an Asset Reference into an immutable algorithm input.
///
/// It validates the reference against the Data Manager (existence, resolvable
/// state, expected revision, required capability) and holds a `LeaseKind::Task`
/// lease for the life of the returned handle, so the input asset cannot be
/// unloaded mid-run. It depends on the Data Manager only.
class ProcessingAssetResolver
{
  public:
    explicit ProcessingAssetResolver( DataManager *dataManager );

    /// Resolve `asset` into an algorithm input, acquiring a `LeaseKind::Task`
    /// lease described by `purpose`. `requiredCapability` may be
    /// `AssetCapability::None` to skip capability gating. Returns structured
    /// diagnostics on rejection.
    Result<ResolvedAsset> resolve( const AssetRef &asset,
                                   const QString &purpose,
                                   AssetCapability requiredCapability =
                                     AssetCapability::None ) const;

  private:
    DataManager *m_dataManager = nullptr; // not owned
};

} // namespace sicnu::data
