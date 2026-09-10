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

  private slots:
    void openDatasetStore();
    void openExperimentStore();
    void onDatasetSelected( int index );
    void onVersionSelected( int row );
    void onExperimentSelected( int index );
    void onRunsSelectionChanged();
    void compareSelectedRuns();

  private:
    void rebuildDatasets();
    void rebuildVersions();
    void rebuildExperiments();
    void rebuildRuns();
    void showMetricJson( const QString &runId );

    // ── 数据集 tab ──
    QTabWidget *m_tabs = nullptr;
    QPushButton *m_openDatasetDb = nullptr;
    QLabel *m_datasetDbLabel = nullptr;
    QComboBox *m_datasetCombo = nullptr;
    QListWidget *m_versionList = nullptr;
    QLabel *m_versionDetail = nullptr;
    QPlainTextEdit *m_samplePreview = nullptr;

    // ── 实验 tab ──
    QPushButton *m_openExperimentDb = nullptr;
    QLabel *m_experimentDbLabel = nullptr;
    QComboBox *m_experimentCombo = nullptr;
    QTableWidget *m_runsTable = nullptr;
    QPushButton *m_compareBtn = nullptr;
    QPlainTextEdit *m_runDetail = nullptr;

    sicnu::dataset::DatasetStore m_datasetStore;     // opened on demand, closed here
    sicnu::experiment::ExperimentStore m_experimentStore;
    QVector<QVariantMap> m_datasets;                 // paged projection rows
    QVector<sicnu::dataset::DatasetVersionRecord> m_versions;
    QVector<sicnu::experiment::Experiment> m_experiments;
    QVector<sicnu::experiment::ExperimentRun> m_runs;
};

} // namespace sicnu::app
