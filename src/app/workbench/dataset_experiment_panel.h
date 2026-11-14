/***************************************************************************
 * dataset_experiment_panel.h — Workbench 7.0 dataset/experiment bench (§E)
 *
 * A deliberately thin client over the ML-engineering stores (sicnu::dataset
 * DatasetStore + sicnu::experiment ExperimentStore — the SAME stores the CLI
 * and agent write). The panel opens the store files the user points it at,
 * reads them through their public paged APIs and renders a projection.
 * It persists nothing and derives nothing: no samples are materialized as
 * widgets (first page + truthful counts), no leakage/quality computation
 * happens in the UI, and every "unknown" field renders as unknown.
 ***************************************************************************/
#pragma once

#include <qgsdockwidget.h>

#include <QJsonObject>
#include <QPointer>
#include <QStringList>

#include <optional>

#include "dataset/dataset_store.h"
#include "experiment/experiment_store.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QPlainTextEdit;

namespace sicnu::app
{

class DatasetExperimentPanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit DatasetExperimentPanel( QWidget *parent = nullptr );

    bool openDatasetStoreAtPath( const QString &path );
    bool openExperimentStoreAtPath( const QString &path );

  signals:
    /// Workbench 10.0 unified selection sources: the shell forwards these to
    /// SelectionContext (experiment runs → selectedExperimentIds, datasets →
    /// selectedDatasetIds). Ids are the stores' authoritative ids.
    void experimentRunSelectionChanged( const QStringList &runIds );
    void datasetSelectionChanged( const QString &datasetId );

  private slots:
    void openDatasetStore();
    void openExperimentStore();
    void onDatasetSelected( int index );
    void onVersionSelected( int row );
    void onExperimentSelected( int index );
    void onRunsSelectionChanged();
    void compareSelectedRuns();
    /// R30-T07 WP06: explicit, stable comparison pins.
    void pinRunA();
    void pinRunB();
    void clearComparisonPins();

  private:
    void rebuildDatasets();
    void rebuildVersions();
    void rebuildExperiments();
    void rebuildRuns();
    /// Resolves the selection memory against the visible rows: one place
    /// that (re)enables compare and emits the shell signal when the
    /// resolvable set changed. Called on EVERY rebuild exit path.
    void settleRunsSelection();
    /// Clears rows/pagers after a failed store (re)open so no stale
    /// projection of the previous store remains under an error label.
    void resetDatasetViews();
    void resetExperimentViews();
    void showMetricJson( const QString &runId );
    /// Resolves a run's dataset version → manifest.label_schema → the REAL
    /// stored LabelSchema. Returns a typed evidence object; never guesses.
    QJsonObject labelSchemaEvidenceForRun( const QString &runId,
                                           std::optional<sicnu::dataset::LabelSchema> *schemaOut ) const;
    void refreshPinLabel();
    /// Drops pins whose run id is no longer resolvable in the open store.
    void invalidateMissingPins();

    // ── 数据集 tab ──
    QTabWidget *m_tabs = nullptr;
    QPushButton *m_openDatasetDb = nullptr;
    QLabel *m_datasetDbLabel = nullptr;
    QComboBox *m_datasetCombo = nullptr;
    QListWidget *m_versionList = nullptr;
    QLabel *m_versionDetail = nullptr;
    QPlainTextEdit *m_samplePreview = nullptr;
    QPushButton *m_datasetPrevPage = nullptr;
    QPushButton *m_datasetNextPage = nullptr;
    QLabel *m_datasetPageLabel = nullptr;
    QPushButton *m_samplePrevPage = nullptr;
    QPushButton *m_sampleNextPage = nullptr;
    QLabel *m_samplePageLabel = nullptr;

    // ── 实验 tab ──
    QPushButton *m_openExperimentDb = nullptr;
    QLabel *m_experimentDbLabel = nullptr;
    QComboBox *m_experimentCombo = nullptr;
    QTableWidget *m_runsTable = nullptr;
    QPushButton *m_compareBtn = nullptr;
    QPushButton *m_pinABtn = nullptr;
    QPushButton *m_pinBBtn = nullptr;
    QPushButton *m_clearPinsBtn = nullptr;
    QLabel *m_pinLabel = nullptr;
    /// R30-T07 WP06: explicit cross-page pins, resolved by run ID (never by
    /// row number). Empty string = slot free.
    QString m_pinnedRunA;
    QString m_pinnedRunB;
    QPlainTextEdit *m_runDetail = nullptr;
    QPushButton *m_experimentPrevPage = nullptr;
    QPushButton *m_experimentNextPage = nullptr;
    QLabel *m_experimentPageLabel = nullptr;
    QPushButton *m_runPrevPage = nullptr;
    QPushButton *m_runNextPage = nullptr;
    QLabel *m_runPageLabel = nullptr;

    qint64 m_datasetOffset = 0;
    qint64 m_experimentOffset = 0;
    qint64 m_runOffset = 0;
    qint64 m_sampleOffset = 0;
    qint64 m_datasetTotal = 0;
    qint64 m_experimentTotal = 0;
    qint64 m_runTotal = 0;
    qint64 m_sampleTotal = 0;
    /// Version whose sample page m_sampleOffset refers to; switching versions
    /// restarts the sample pager instead of inheriting the old offset.
    QString m_sampleVersionId;

    sicnu::dataset::DatasetStore m_datasetStore;     // opened on demand, closed here
    sicnu::experiment::ExperimentStore m_experimentStore;
    QString m_experimentStorePath;
    QVector<QVariantMap> m_datasets;                 // paged projection rows
    QVector<sicnu::dataset::DatasetVersionRecord> m_versions;
    QVector<sicnu::experiment::Experiment> m_experiments;
    QVector<sicnu::experiment::ExperimentRun> m_runs;
    /// Stable run-id selection memory (D3, R29-T04): keeps the ids the user
    /// selected (including ones currently off-page), re-selecting them
    /// whenever their rows are visible again; only a REAL user selection
    /// change rewrites it.
    QStringList m_selectedRunIds;
    /// What experimentRunSelectionChanged last told the shell — rebuilds
    /// re-emit only when the resolvable set actually changed.
    QStringList m_lastEmittedRunSelection;
};

} // namespace sicnu::app
