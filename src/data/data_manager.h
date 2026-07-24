#pragma once

#include <memory>
#include <optional>

#include <QObject>
#include <QVector>

#include "data_asset.h"
#include "data_result.h"

namespace sicnu::data
{

namespace internal
{
class SourceProviderRegistry;
struct AssetLeaseControl;
}

class DataManager;

class AssetLease
{
  public:
    AssetLease() = default;

    AssetLease( const AssetLease & ) = delete;
    AssetLease &operator=( const AssetLease & ) = delete;

    AssetLease( AssetLease &&other ) noexcept;
    AssetLease &operator=( AssetLease &&other ) noexcept;
    ~AssetLease();

    bool isValid() const;
    const AssetId &assetId() const;
    quint64 token() const;
    LeaseKind kind() const;
    const QString &purpose() const;
    LeaseRef toRef() const;

    /// Releases the lease explicitly. Returns Released on success, Invalid when
    /// already released or detached. After this call isValid() is false.
    LeaseOutcome release();

  private:
    friend class DataManager;

    explicit AssetLease( std::shared_ptr<internal::AssetLeaseControl> control );

    std::shared_ptr<internal::AssetLeaseControl> m_control;
};

class DataManager : public QObject
{
    Q_OBJECT

  public:
    explicit DataManager( QObject *parent = nullptr );
    ~DataManager() override;

    RegisterResult registerSource( const RegisterRequest &request );
    Result<AssetId> restoreSource( const RestoreRequest &request );
    Result<RelocateResult> relocate( const RelocateRequest &request );
    std::optional<AssetSnapshot> asset( AssetId id ) const;
    QVector<AssetSnapshot> assets( const AssetQuery &query = {} ) const;

    quint64 catalogGeneration() const;

    Result<AssetLease> acquire( const AssetRef &asset, const AssetUse &use );
    int leaseCount( AssetId id ) const;
    QVector<LeaseRef> leases( AssetId id ) const;
    /// True while an active Edit Lease exists for the asset (some view is editing).
    bool hasActiveEditLease( AssetId id ) const;

    /// Commits the active Edit Lease for a Vector Asset: advances the asset
    /// revision, releases the Edit Lease, and emits one assetChanged so other
    /// Display Layers refresh. Fails if no active Edit Lease exists.
    Result<void> commitEdit( AssetId id );
    /// Rolls back the active Edit Lease without advancing the revision. The Edit
    /// Lease is released and no change event is emitted.
    Result<void> rollbackEdit( AssetId id );

    UnloadPlan planUnload( AssetId id ) const;
    Result<void> unload( const UnloadPlan &confirmedPlan );

  signals:
    void assetAdded( AssetId id );
    void assetChanged( AssetId id );
    void assetAboutToUnload( AssetId id );
    void assetRemoved( AssetId id );

  private:
    friend class internal::SourceProviderRegistry;
    friend class AssetLease;

    explicit DataManager( std::unique_ptr<internal::SourceProviderRegistry> providers,
                          QObject *parent = nullptr );

    /// Registry seeded with the built-in local GDAL raster and OGR vector
    /// providers. Used by the public constructor so the application resolves real
    /// sources by default; tests inject their own registry to stay hermetic.
    static std::unique_ptr<internal::SourceProviderRegistry> defaultProviders();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    LeaseOutcome releaseLease( const LeaseRef &lease );
    void revokeLease( const LeaseRef &lease );
};

} // namespace sicnu::data
