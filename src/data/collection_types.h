#pragma once

#include <optional>

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QUuid>
#include <QVector>
#include <QtTypes>

#include "asset_types.h"
#include "data_result.h"

namespace sicnu::data
{

/// Identity of a Data Collection, parallel to AssetId. A collection is an
/// organizational catalog node grouping child Data Assets; it is not itself
/// renderable or processable.
class CollectionId
{
  public:
    CollectionId() = default;

    static CollectionId generate();
    static std::optional<CollectionId> fromString( const QString &text );

    bool isNull() const;
    QString toString() const;

    friend bool operator==( const CollectionId &, const CollectionId & ) = default;

  private:
    explicit CollectionId( QUuid value );

    QUuid m_value;
};

/// Normalized remote-sensing product metadata carried by a collection. This is
/// the domain model, mapped from provider inputs (e.g. SatelliteProducts::
/// ProductInfo), not the raw provider blob.
struct ProductMetadata
{
  QString platform;
  QString sensor;
  QString productLevel;
  QString acquisitionDate;
  QString processingLevel;
  /// Free-form provider-specific attributes (e.g. tile id, path/row).
  QMap<QString, QString> attributes;

  friend bool operator==( const ProductMetadata &, const ProductMetadata & ) = default;
};

/// A read-only snapshot of a Data Collection in the catalog.
struct CollectionSnapshot
{
  CollectionId id;
  QString displayName;
  ProductMetadata metadata;
  /// Ordered child asset ids. A child reaped/unloaded independently is removed
  /// from this list.
  QVector<AssetId> childAssetIds;
};

/// Request to create a collection.
struct CollectionCreateRequest
{
  QString displayName;
  ProductMetadata metadata;
};

/// Result of creating a collection.
struct CollectionCreateResult
{
  CollectionId collectionId;
  QVector<Diagnostic> diagnostics;
};

} // namespace sicnu::data
