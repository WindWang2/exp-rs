#pragma once

#include <memory>
#include <optional>

#include <QObject>
#include <QVector>

#include "data_asset.h"

namespace sicnu::data
{

namespace internal
{
class SourceProviderRegistry;
}

class DataManager : public QObject
{
    Q_OBJECT

  public:
    explicit DataManager( QObject *parent = nullptr );
    ~DataManager() override;

    RegisterResult registerSource( const RegisterRequest &request );
    std::optional<AssetSnapshot> asset( AssetId id ) const;
    QVector<AssetSnapshot> assets( const AssetQuery &query = {} ) const;

  signals:
    void assetAdded( AssetId id );

  private:
    friend class internal::SourceProviderRegistry;

    explicit DataManager( std::unique_ptr<internal::SourceProviderRegistry> providers,
                          QObject *parent = nullptr );

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sicnu::data
