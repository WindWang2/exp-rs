#include "processing_cache.h"

#include <QCoreApplication>
#include <QFile>

namespace sicnu {

ProcessingCache::ProcessingCache(const QString &cacheDir)
    : m_cacheDir(cacheDir.isEmpty() ? QCoreApplication::applicationDirPath() + "/cache" : cacheDir)
    , m_maxSizeBytes(1024 * 1024 * 100) // 100MB default
{
}

bool ProcessingCache::contains(const QString &key) const
{
    return QFile::exists(cachePath(key));
}

bool ProcessingCache::store(const QString &key, const QByteArray &data)
{
    QFile file(cachePath(key));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    qint64 written = file.write(data);
    file.close();
    return written == data.size();
}

QByteArray ProcessingCache::retrieve(const QString &key) const
{
    QFile file(cachePath(key));
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

bool ProcessingCache::remove(const QString &key)
{
    return QFile::remove(cachePath(key));
}

void ProcessingCache::clear()
{
    QDir dir(m_cacheDir);
    dir.removeRecursively();
    dir.mkpath(".");
}

int ProcessingCache::size() const
{
    QDir dir(m_cacheDir);
    return dir.entryList(QDir::Files).size();
}

qint64 ProcessingCache::maxSizeBytes() const
{
    return m_maxSizeBytes;
}

void ProcessingCache::setMaxSizeBytes(qint64 size)
{
    m_maxSizeBytes = size;
}

QString ProcessingCache::cachePath(const QString &key) const
{
    return m_cacheDir + "/" + key + ".cache";
}

} // namespace sicnu
