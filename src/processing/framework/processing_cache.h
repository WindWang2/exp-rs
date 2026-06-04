#ifndef PROCESSING_CACHE_H
#define PROCESSING_CACHE_H

#include <QString>
#include <QByteArray>
#include <QDir>

namespace sicnu {

class ProcessingCache
{
public:
    explicit ProcessingCache(const QString &cacheDir = QString());

    bool contains(const QString &key) const;
    bool store(const QString &key, const QByteArray &data);
    QByteArray retrieve(const QString &key) const;
    bool remove(const QString &key);
    void clear();
    int size() const;

    qint64 maxSizeBytes() const;
    void setMaxSizeBytes(qint64 size);

private:
    QString cachePath(const QString &key) const;

    QString m_cacheDir;
    qint64 m_maxSizeBytes;
};

} // namespace sicnu

#endif // PROCESSING_CACHE_H
