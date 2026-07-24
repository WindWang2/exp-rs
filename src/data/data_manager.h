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
}

class DataManager;

class AssetLease
{
  public:
    AssetLease() = default;

    AssetLease( const AssetLease & ) = delete;
    AssetLease &operator=( const AssetLease & ) = delete;

    AssetLease( AssetLease &&other ) noexcept
      : m_assetId( other.m_assetId )
      , m_token( other.m_token )
      , m_kind( other.m_kind )
      , m_purpose( std::move( other.m_purpose ) )
      , m_manager( other.m_manager )
    {
      other.m_manager = nullptr;
      other.m_token = 0;
    }

    AssetLease &operator=( AssetLease &&other ) noexcept
    {
      if ( this == &other )
        return *this;

      release();

      m_assetId = other.m_assetId;
      m_token = other.m_token;
      m_kind = other.m_kind;
      m_purpose = std::move( other.m_purpose );
      m_manager = other.m_manager;
      other.m_manager = nullptr;
      other.m_token = 0;
      return *this;
    }

    ~AssetLease()
    {
      release();
    }

    bool isValid() const
    {
      return m_manager != nullptr && m_token != 0;
    }

    const AssetId &assetId() const
    {
      return m_assetId;
    }

    quint64 token() const
    {
      return m_token;
    }

    LeaseKind kind() const
    {
      return m_kind;
    }

    const QString &purpose() const
    {
      return m_purpose;
    }

    LeaseRef toRef() const
    {
      return LeaseRef{ m_assetId, m_token, m_kind };
    }

    /// Releases the lease explicitly. Returns Released on success, Invalid when
    /// already released or detached. After this call isValid() is false.
    LeaseOutcome release();

  private:
    friend class DataManager;

    AssetLease( AssetId assetId,
                quint64 token,
                LeaseKind kind,
                QString purpose,
                DataManager *manager )
      : m_assetId( assetId )
      , m_token( token )
      , m_kind( kind )
      , m_purpose( std::move( purpose ) )
      , m_manager( manager )
    {
    }

    /// Detaches ownership without notifying the manager. Used by the manager
    /// when it reclaims the lease itself during unload so release() is a no-op.
    void detach();

    AssetId m_assetId;
    quint64 m_token = 0;
    LeaseKind m_kind = LeaseKind::View;
    QString m_purpose;
    DataManager *m_manager = nullptr;
};

class DataManager : public QObject
{
    Q_OBJECT

  public:
    explicit DataManager( QObject *parent = nullptr );
    ~DataManager() override;

    RegisterResult registerSource( const RegisterRequest &request );
    std::optional<AssetSnapshot> asset( AssetId id ) const;
    QVector<AssetSnapshot> assets( const AssetQuery &query = {} ) const;

    quint64 catalogGeneration() const;

    Result<AssetLease> acquire( const AssetRef &asset, const AssetUse &use );
    int leaseCount( AssetId id ) const;
    QVector<LeaseRef> leases( AssetId id ) const;

    UnloadPlan planUnload( AssetId id ) const;
    Result<void> unload( const UnloadPlan &confirmedPlan );

  signals:
    void assetAdded( AssetId id );
    void assetAboutToUnload( AssetId id );
    void assetRemoved( AssetId id );

  private:
    friend class internal::SourceProviderRegistry;
    friend class AssetLease;

    explicit DataManager( std::unique_ptr<internal::SourceProviderRegistry> providers,
                          QObject *parent = nullptr );

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    LeaseOutcome releaseLease( const LeaseRef &lease );
    void detachLease( const LeaseRef &lease );
};

} // namespace sicnu::data
