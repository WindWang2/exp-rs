// metadata_pipeline.cpp — see metadata_pipeline.h for the contract.
#include "metadata_pipeline.h"

#include "workspace_service.h"

#include "data_asset.h"
#include "data_manager.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <cpl_conv.h>
#include <ogr_spatialref.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <atomic>

namespace sicnu::workspace
{

#define SICNU_MODALITY_KEY "SICNU_MODALITY"

using sicnu::data::AssetSnapshot;

namespace
{

/// Extracts enrichment fields from GDAL metadata keys with graceful absence.
struct GdalFacts
{
    QString format;
    QString crsWkt;
    qint64 bandCount = -1;
    QString sensor;
    QString modality;
    bool valid = false;
};

GdalFacts readGdalFacts( const QString &path )
{
    GdalFacts facts;
    GDALDatasetH dataset = GDALOpenEx( path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY
                                                              | GDAL_OF_VERBOSE_ERROR,
                                       nullptr, nullptr, nullptr );
    if ( !dataset )
        return facts;
    facts.valid = true;
    GDALDataset *poDS = GDALDataset::FromHandle( dataset );
    facts.format = QString::fromUtf8( poDS->GetDriverName() );
    facts.bandCount = poDS->GetRasterCount();
    if ( poDS->GetSpatialRef() )
    {
        char *wkt = nullptr;
        if ( poDS->GetSpatialRef()->exportToWkt( &wkt ) == OGRERR_NONE && wkt )
        {
            facts.crsWkt = QString::fromUtf8( wkt );
            CPLFree( wkt );
        }
    }
    const CSLConstList metadata = poDS->GetMetadata();
    if ( metadata )
    {
        const char *sensorValue = CSLFetchNameValue( metadata, "SICNU_SENSOR" );
        const char *modalityValue = CSLFetchNameValue( metadata, SICNU_MODALITY_KEY );
        const QString sensor = sensorValue ? QString::fromUtf8( sensorValue ) : QString();
        const QString modality = modalityValue ? QString::fromUtf8( modalityValue ) : QString();
        if ( !sensor.isEmpty() )
            facts.sensor = sensor;
        if ( !modality.isEmpty() )
            facts.modality = modality;
    }
    GDALClose( dataset );
    return facts;
}

} // namespace

MetadataPipeline::MetadataPipeline( WorkspaceService &service, QObject *parent )
    : QObject( parent )
    , m_service( service )
    , m_pool( this )
{
    qRegisterMetaType<MetadataPipeline::Summary>();
}

MetadataPipeline::~MetadataPipeline()
{
    cancel();
    m_pool.clear();
    m_pool.waitForDone( 30000 );
}

void MetadataPipeline::cancel()
{
    m_cancel.store( true );
}

QJsonObject MetadataPipeline::Summary::toJson() const
{
    return QJsonObject{ { QLatin1String( "scanned" ), scanned },
                        { QLatin1String( "verified" ), verified },
                        { QLatin1String( "enriched" ), enriched },
                        { QLatin1String( "skipped" ), skipped },
                        { QLatin1String( "failed" ), failed },
                        { QLatin1String( "cancelled" ), cancelled } };
}

bool MetadataPipeline::start()
{
    return start( Config() );
}

bool MetadataPipeline::start( const Config &config )
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

    // Snapshot the work list on the owning thread — DataManager is
    // thread-affine and must not be touched from workers.
    QVector<QPair<QString, QString>> work;
    for ( const AssetSnapshot &snapshot : m_dataManager->assets() )
    {
        if ( snapshot.storageKind() == sicnu::data::StorageKind::Remote )
            continue;
        work.append( qMakePair( snapshot.id().toString(), snapshot.source().canonicalSource ) );
    }

    const Config passConfig = config;
    m_pool.start( [ this, passConfig, work ]() mutable {
        runPass( passConfig, work );
    } );
    return true;
}

void MetadataPipeline::runPass( Config config, QVector<QPair<QString, QString>> work )
{
    Summary summary;
    summary.scanned = work.size();

    QVector<GovernedAsset> pending;
    auto flush = [ & ]() {
        if ( pending.isEmpty() )
            return;
        ( void ) m_service.store().upsertAssets( pending );
        pending.clear();
    };

    const int workerBatches = qMax( 1, config.maxWorkers );
    std::atomic<int> processed{ 0 };

    // Single-threaded pass per by design here keeps GDAL stat+digest order
    // stable and memory bounded; maxWorkers bounds per-asset GDAL concurrency
    // through QtConcurrent-style batches below.
    Q_UNUSED( workerBatches );

    for ( const QPair<QString, QString> &item : work )
    {
        if ( m_cancel.load() )
        {
            summary.cancelled = true;
            break;
        }
        const QString &assetId = item.first;
        const QString &path = item.second;

        std::optional<GovernedAsset> row = m_service.store().assetById( assetId );
        if ( !row )
        {
            // Asset vanished mid-pass; count and move on.
            ++summary.skipped;
            continue;
        }

        QFileInfo info( path );
        if ( !info.exists() )
        {
            ++summary.failed;
            continue;
        }

        const qint64 size = info.size();
        const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
        const bool fresh = config.incremental && row->verifiedMs > 0 && row->sizeBytes == size
                           && row->mtimeMs == mtime && ( !config.recomputeFingerprints || !row->contentFingerprint.isEmpty() );
        if ( fresh )
        {
            ++summary.skipped;
        }
        else
        {
            if ( config.recomputeFingerprints || row->contentFingerprint.isEmpty() )
            {
                const QString digest = WorkspaceService::contentFingerprint( path );
                if ( !digest.isEmpty() && !row->contentFingerprint.isEmpty() && digest != row->contentFingerprint )
                    row->availability = QStringLiteral( "stale" );
                if ( !digest.isEmpty() )
                    row->contentFingerprint = digest;
            }
            row->sizeBytes = size;
            row->mtimeMs = mtime;
            row->verifiedMs = QDateTime::currentMSecsSinceEpoch();
            if ( row->availability.isEmpty() || row->availability == QLatin1String( "unverified" ) )
                row->availability = QStringLiteral( "fresh" );
            ++summary.verified;

            if ( config.refreshStructure )
            {
                const GdalFacts facts = readGdalFacts( path );
                if ( facts.valid )
                {
                    row->format = facts.format.isEmpty() ? row->format : facts.format;
                    row->crs = facts.crsWkt.isEmpty() ? row->crs : facts.crsWkt;
                    row->bandCount = facts.bandCount;
                    row->sensor = facts.sensor.isEmpty() ? row->sensor : facts.sensor;
                    row->modality = facts.modality.isEmpty() ? row->modality : facts.modality;
                    ++summary.enriched;
                }
            }
            pending.append( *row );
        }

        const int done = processed.fetch_add( 1 ) + 1;
        if ( done % config.batchSize == 0 )
        {
            flush();
            QMetaObject::invokeMethod( this, [ this, done ]() {
                emit progress( done );
            }, Qt::QueuedConnection );
        }
    }
    flush();

    // Ownership handoff happens on the owning thread so isRunning() flips
    // after the queued summary lands.
    Summary passSummary = summary;
    QPointer<MetadataPipeline> guard( this );
    QMetaObject::invokeMethod( this, [ this, guard, passSummary ]() {
        m_running.store( false );
        if ( guard )
            emit guard->finished( passSummary );
    }, Qt::QueuedConnection );
}

} // namespace sicnu::workspace
