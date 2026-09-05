// import_center.h — unified import entry (Platform 3.0 Phase R).
//
// Flow per asset: detect → inspect → validate → deduplicate → (register on
// the owning thread) → index. Large directory scans are incremental,
// cancellable and bounded: discovery caps at maxFiles, registration batches
// marshal onto the DataManager thread, and open files stay bounded by design
// (extension-based detection, GDAL opens only happen later through the
// normal registration providers).
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <QThreadPool>

#include <atomic>

class QJsonObject;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::workspace
{

class WorkspaceService;

struct ImportScanOptions
{
    QString root;                     ///< directory to scan (or a single file)
    bool recursive = true;
    QStringList extensions;           ///< empty = default raster/vector set
    qint64 maxFiles = 100000;         ///< discovery bound
    int registrationBatch = 64;       ///< owning-thread registration chunk
};

struct ImportScanReport
{
    int discovered = 0;
    int registered = 0;
    int duplicates = 0;
    int skipped = 0;
    int failed = 0;
    bool cancelled = false;   ///< user-initiated cancellation only
    bool truncated = false;   ///< discovery bound exceeded (not a cancellation)
    QJsonObject toJson() const;
};

class ImportCenter : public QObject
{
    Q_OBJECT

  public:
    explicit ImportCenter( WorkspaceService &service, QObject *parent = nullptr );
    ~ImportCenter() override;

    void bindDataManager( sicnu::data::DataManager *manager ) { m_dataManager = manager; }

    bool startScan( const ImportScanOptions &options );
    bool startScan() { return startScan( ImportScanOptions() ); }
    /// Registers remote locators (COG/http(s) URLs, pre-resolved STAC asset
    /// hrefs) as governed remote assets. Registration is cheap by design —
    /// the GDAL provider defers network opens off the calling thread — so
    /// this runs synchronously on the owning thread.
    ImportScanReport importRemote( const QStringList &urls );
    void cancel();
    bool isRunning() const { return m_running.load(); }

  signals:
    void progress( int processed );
    void finished( const sicnu::workspace::ImportScanReport &report );

  private:
    int m_importedThisBatch = 0;
    int m_failedThisBatch = 0;

    WorkspaceService &m_service;
    sicnu::data::DataManager *m_dataManager = nullptr;
    std::atomic_bool m_running{ false };
    std::atomic_bool m_cancel{ false };
    /// Own pool (see MetadataPipeline): bounded lifetime, drained in dtor.
    QThreadPool m_pool;
};

} // namespace sicnu::workspace
