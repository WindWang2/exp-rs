// metadata_pipeline.h — async, bounded, incremental metadata extraction
// (Platform 3.0 Phase G).
//
// Collects the work list on the owning thread (DataManager is thread-affine),
// then runs stat/verify/enrichment on a bounded worker pool. Workers only
// touch the GovernanceStore (mutex-guarded); every catalog-visible mutation
// is announced back on the owning thread through queued signals. Cancellation
// is cooperative and checked per asset.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVector>

#include <atomic>

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::workspace
{

class WorkspaceService;

class MetadataPipeline : public QObject
{
    Q_OBJECT

  public:
    struct Config
    {
        int maxWorkers = 2;              ///< bounded concurrency (RAM/disk friendly)
        int batchSize = 256;             ///< progress signal + store batch size
        bool recomputeFingerprints = false;  ///< stream SHA-256 over file contents
        bool refreshStructure = false;   ///< GDAL-open for format/CRS/bands/sensor
        bool incremental = true;         ///< skip assets already verified fresh
    };

    struct Summary
    {
        int scanned = 0;
        int verified = 0;
        int enriched = 0;
        int skipped = 0;
        int failed = 0;
        bool cancelled = false;

        QJsonObject toJson() const;
    };

    explicit MetadataPipeline( WorkspaceService &service, QObject *parent = nullptr );
    ~MetadataPipeline() override;

    void bindDataManager( sicnu::data::DataManager *manager ) { m_dataManager = manager; }

    /// Starts an asynchronous pass over the current catalog. Returns false
    /// when a pass is already running (single-flight by design).
    bool start();
    bool start( const Config &config );
    void cancel();
    bool isRunning() const { return m_running.load(); }

  signals:
    void progress( int processed );
    void finished( const sicnu::workspace::MetadataPipeline::Summary &summary );

  private:
    void runPass( Config config, QVector<QPair<QString, QString>> work );

    WorkspaceService &m_service;
    sicnu::data::DataManager *m_dataManager = nullptr;
    std::atomic_bool m_running{ false };
    std::atomic_bool m_cancel{ false };
    /// Own pool: the destructor drains exactly this pipeline's workers, so
    /// destroy-while-running can never leave a worker touching dead members
    /// (review P1-27) nor hang on the global pool.
    QThreadPool m_pool;
};

} // namespace sicnu::workspace

Q_DECLARE_METATYPE( sicnu::workspace::MetadataPipeline::Summary )
