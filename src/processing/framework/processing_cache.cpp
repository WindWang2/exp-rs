#include "processing_cache.h"
#include "core/sicnu_logging.h"

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QMutexLocker>
#include <QDirIterator>
#include <QDebug>

namespace sicnu {

ProcessingCache::ProcessingCache(const QString &cacheDir)
    : m_cacheDir(cacheDir.isEmpty() ? QCoreApplication::applicationDirPath() + "/cache" : cacheDir)
    , m_maxSizeBytes(1024 * 1024 * 100) // 100MB default
{
}

bool ProcessingCache::contains(const QString &key) const
{
    QMutexLocker locker(&m_mutex);
    return QFile::exists(cachePath(key));
}

bool ProcessingCache::store(const QString &key, const QByteArray &data)
{
    QMutexLocker locker(&m_mutex);

    qint64 currentSize = currentCacheSizeBytes();
    if (currentSize + data.size() > m_maxSizeBytes)
    {
        SICNU_LOG_WARN(SicnuLogTags::Framework, QString("Cache size limit exceeded (%1 + %2 > %3 bytes)")
            .arg(currentSize).arg(data.size()).arg(m_maxSizeBytes));
        return false;
    }

    QDir dir(m_cacheDir);
    if (!dir.exists())
        dir.mkpath(".");

    // QSaveFile handles atomic write internally (temp file + rename on commit)
    const QString targetPath = cachePath(key);

    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    qint64 written = file.write(data);
    if (written != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QByteArray ProcessingCache::retrieve(const QString &key) const
{
    QMutexLocker locker(&m_mutex);
    QFile file(cachePath(key));
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

bool ProcessingCache::remove(const QString &key)
{
    QMutexLocker locker(&m_mutex);
    return QFile::remove(cachePath(key));
}

void ProcessingCache::clear()
{
    QMutexLocker locker(&m_mutex);
    QDir dir(m_cacheDir);
    if (!dir.removeRecursively())
    {
        qWarning("ProcessingCache: failed to remove cache directory: %s",
                 qPrintable(m_cacheDir));
    }
    dir.mkpath(".");
}

int ProcessingCache::size() const
{
    QMutexLocker locker(&m_mutex);
    QDir dir(m_cacheDir);
    return dir.entryList(QDir::Files).size();
}

qint64 ProcessingCache::maxSizeBytes() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxSizeBytes;
}

void ProcessingCache::setMaxSizeBytes(qint64 size)
{
    QMutexLocker locker(&m_mutex);
    m_maxSizeBytes = size;
}

QString ProcessingCache::cachePath(const QString &key) const
{
    QString hashedKey = QString::fromUtf8(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
    return m_cacheDir + "/" + hashedKey + ".cache";
}

qint64 ProcessingCache::currentCacheSizeBytes() const
{
    qint64 totalSize = 0;
    QDirIterator it(m_cacheDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        totalSize += it.fileInfo().size();
    }
    return totalSize;
}

} // namespace sicnu
