#pragma once

#include <QMap>
#include <QString>

namespace sicnu::data
{

class SourceKey
{
  public:
    friend bool operator==( const SourceKey &, const SourceKey & ) = default;

  private:
    friend struct SourceDescriptor;

    SourceKey( QString providerKey,
               QString canonicalSource,
               QString subdataset,
               QMap<QString, QString> dataOptions );

    QString m_providerKey;
    QString m_canonicalSource;
    QString m_subdataset;
    QMap<QString, QString> m_dataOptions;
};

struct SourceDescriptor
{
  QString providerKey;
  QString canonicalSource;
  QString subdataset;
  QMap<QString, QString> dataOptions;
  QString authConfigId;

  SourceKey sourceKey() const;
};

} // namespace sicnu::data
