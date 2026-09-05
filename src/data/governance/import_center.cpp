// import_center.cpp — see import_center.h for the contract.
#include "import_center.h"

#include "workspace_service.h"

#include "data_manager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

namespace sicnu::workspace
{

namespace
{

QStringList defaultExtensions()
{
    return QStringList{
        // rasters
        QStringLiteral( "tif" ), QStringLiteral( "tiff" ), QStringLiteral( "tif+" ),
        QStringLiteral( "img" ), QStringLiteral( "ecw" ), QStringLiteral( "jp2" ),
        QStringLiteral( "nc" ), QStringLiteral( "hdf" ), QStringLiteral( "grd" ),
        QStringLiteral( "vrt" ), QStringLiteral( "dem" ), QStringLiteral( "asc" ),
        // vectors
        QStringLiteral( "shp" ), QStringLiteral( "gpkg" ), QStringLiteral( "geojson" ),
        QStringLiteral( "json" ), QStringLiteral( "gml" ), QStringLiteral( "kml" ),
        QStringLiteral( "tab" ), QStringLiteral( "csv" ),
    };
}

bool suffixMatches( const QString &path, const QStringList &extensions )
{
    const QString suffix = QFileInfo( path ).suffix().toLower();
    return !suffix.isEmpty() && extensions.contains( suffix );
}

bool vectorSuffix( const QString &path )
{
    static const QSet<QString> kVector = {
        QStringLiteral( "shp" ), QStringLiteral( "gpkg" ), QStringLiteral( "geojson" ),
        QStringLiteral( "json" ), QStringLiteral( "gml" ), QStringLiteral( "kml" ),
        QStringLiteral( "tab" ),
    };
    const QString suffix = QFileInfo( path ).suffix().toLower();
    return kVector.contains( suffix );
}

} // namespace

ImportCenter::ImportCenter( WorkspaceService &service, QObject *parent )
    : QObject( parent )
    , m_service( service )
    , m_pool( this )
{
    qRegisterMetaType<ImportScanReport>();
}

ImportCenter::~ImportCenter()
{
    cancel();
    m_pool.clear();
    m_pool.waitForDone( 30000 );
}

void ImportCenter::cancel()
{
    m_cancel.store( true );
}

QJsonObject ImportScanReport::toJson() const
{
    return QJsonObject{ { QLatin1String( "discovered" ), discovered },
                        { QLatin1String( "registered" ), registered },
                        { QLatin1String( "duplicates" ), duplicates },
                        { QLatin1String( "skipped" ), skipped },
                        { QLatin1String( "failed" ), failed },
                        { QLatin1String( "cancelled" ), cancelled },
                        { QLatin1String( "truncated" ), truncated } };
}

ImportScanReport ImportCenter::importRemote( const QStringList &urls )
{
    ImportScanReport report;
    if ( !m_dataManager )
        return report;
    for ( const QString &rawUrl : urls )
    {
        const QString url = rawUrl.trimmed();
        if ( url.isEmpty() )
            continue;
        // The GDAL provider normalizes http(s) to /vsicurl/ at resolve time,
        // so the durable alias carries the NORMALIZED spelling — check both
        // plus the runtime authority before treating a URL as new.
        const QString normalized = url.startsWith( QLatin1String( "http" ) )
                                       ? QStringLiteral( "/vsicurl/" ) + url
                                       : url;
        const bool known = m_service.store().assetByPath( url ).has_value()
                           || m_service.store().assetByPath( normalized ).has_value()
                           || m_dataManager->findByPath( url ).has_value();
        if ( known )
        {
            ++report.duplicates;
            continue;
        }
        sicnu::data::RegisterRequest request;
        request.source.providerKey = QStringLiteral( "gdal" );
        request.source.canonicalSource = url;
        const sicnu::data::RegisterResult result = m_dataManager->registerSource( request );
        if ( result.assetId.isNull() )
            ++report.failed;
        else if ( result.reusedExisting )
            ++report.duplicates;  // runtime dedup caught what the index missed
        else
        {
            ++report.registered;
            ++report.discovered;
        }
    }
    m_service.audit( QStringLiteral( "import" ), QStringLiteral( "import.remote" ),
                     QStringLiteral( "workspace" ), QString(),
                     QJsonObject{ { QLatin1String( "registered" ), report.registered },
                                  { QLatin1String( "duplicates" ), report.duplicates } } );
    return report;
}

bool ImportCenter::startScan( const ImportScanOptions &options )
{
    bool expected = false;
    if ( !m_running.compare_exchange_strong( expected, true ) )
        return false;
    m_cancel.store( false );
    if ( !m_dataManager )
    {
        m_running.store( false );
        return false;
    }

    ImportScanOptions opts = options;
    if ( opts.extensions.isEmpty() )
        opts.extensions = defaultExtensions();

    QPointer<ImportCenter> guard( this );
    m_pool.start( [ this, guard, opts ]() mutable {
        ImportScanReport report;

        // ---- discover + detect + validate (worker) ---------------------------
        QStringList candidates;
        const QFileInfo rootInfo( opts.root );
        if ( rootInfo.isFile() )
        {
            if ( rootInfo.size() > 0 && suffixMatches( opts.root, opts.extensions ) )
            {
                ++report.discovered;
                candidates.append( opts.root );
            }
            else
                ++report.skipped;
        }
        else if ( rootInfo.isDir() )
        {
            QDirIterator it( opts.root,
                             opts.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags );
            while ( it.hasNext() )
            {
                if ( m_cancel.load() )
                {
                    report.cancelled = true;
                    break;
                }
                const QString path = it.next();
                const QFileInfo info( path );
                if ( !info.isFile() || info.size() <= 0 )
                    continue;
                if ( !suffixMatches( path, opts.extensions ) )
                {
                    ++report.skipped;
                    continue;
                }
                ++report.discovered;
                if ( report.discovered > opts.maxFiles )
                {
                    // Bound exceeded: stop DISCOVERY only — collected
                    // candidates still register (review P1-28: never conflate
                    // the discovery bound with a user cancellation).
                    report.truncated = true;
                    break;
                }
                candidates.append( path );
            }
        }

        // ---- deduplicate against the durable index, chunked ------------------
        const int batch = qMax( 1, opts.registrationBatch );
        for ( int offset = 0; offset < candidates.size(); offset += batch )
        {
            if ( m_cancel.load() )
                report.cancelled = true;
            QStringList chunk;
            const int end = qMin( offset + batch, candidates.size() );
            for ( int i = offset; i < end; ++i )
            {
                const QString path = QDir::cleanPath( candidates.at( i ) );
                if ( m_service.store().assetByPath( path ) )
                {
                    ++report.duplicates;
                    continue;
                }
                chunk.append( path );
            }
            if ( chunk.isEmpty() )
                continue;
            // Registration runs on the owning thread (DataManager affinity);
            // the tally comes back via the queued lambda's return path below.
            QMetaObject::invokeMethod( this, [ this, guard, chunk ]() {
                if ( !guard )
                    return;
                for ( const QString &path : chunk )
                {
                    sicnu::data::RegisterRequest request;
                    request.source.providerKey = vectorSuffix( path ) ? QStringLiteral( "ogr" ) : QStringLiteral( "gdal" );
                    request.source.canonicalSource = path;
                    const sicnu::data::RegisterResult result = m_dataManager->registerSource( request );
                    if ( result.assetId.isNull() )
                        ++guard->m_failedThisBatch;
                    else
                        ++guard->m_importedThisBatch;
                }
            }, Qt::BlockingQueuedConnection );
            report.registered += m_importedThisBatch;
            report.failed += m_failedThisBatch;
            m_importedThisBatch = 0;
            m_failedThisBatch = 0;
            QMetaObject::invokeMethod( this, [ this, guard, offset ]() {
                if ( guard )
                    emit guard->progress( offset );
            }, Qt::QueuedConnection );
        }

        ImportScanReport finalReport = report;
        QMetaObject::invokeMethod( this, [ this, guard, finalReport ]() {
            m_running.store( false );
            if ( guard )
                emit guard->finished( finalReport );
        }, Qt::QueuedConnection );
    } );
    return true;
}

} // namespace sicnu::workspace
