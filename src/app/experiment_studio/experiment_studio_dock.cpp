#include "experiment_studio/experiment_studio_dock.h"
#include "experiment_studio/sensitivity_chart_widget.h"

#include "experiment_studio/fault_teaching_projection.h"
#include "experiment_studio/first_divergence_projection.h"
#include "experiment_studio/run_matrix_projection.h"
#include "experiment_studio/sensitivity_projection.h"
#include "experiment_studio/spatial_compare_projection.h"
#include "experiment_studio/studio_export.h"
#include "experiment_studio/study_designer.h"
#include "study/study_export.h"
#include "study/study_spatial.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

using sicnu::app::experiment_studio::ChartSeriesPoint;
using sicnu::app::experiment_studio::SensitivityChartWidget;
using namespace sicnu::experiment_studio;
using namespace sicnu::study;

namespace sicnu::app
{

namespace
{

class BufferSummarizer final : public ISpatialDifferenceSummarizer
{
  public:
    Result<SpatialDifferenceSummary> summarize( const QString &baselinePath,
                                                const QString &runPath ) const override
    {
        // Tiny synthetic buffers for teaching demos when paths are synthetic://
        if ( baselinePath.startsWith( QStringLiteral( "synthetic://" ) )
             || runPath.startsWith( QStringLiteral( "synthetic://" ) ) )
        {
            if ( baselinePath.contains( QStringLiteral( "mismatch" ) )
                 || runPath.contains( QStringLiteral( "mismatch" ) ) )
            {
                return Result<SpatialDifferenceSummary>::failure(
                    studyError( QStringLiteral( "study.spatial_mismatch" ),
                                QStringLiteral( "grids differ (demo refusal)" ) ) );
            }
            const float a[4] = { 0.f, 1.f, 2.f, 3.f };
            const float b[4] = { 0.f, 1.f, 2.5f, 3.f };
            return Result<SpatialDifferenceSummary>::success(
                summarizeBufferDifference( baselinePath, runPath, a, b, 4, 0.0 ) );
        }
        return Result<SpatialDifferenceSummary>::failure(
            studyError( QStringLiteral( "experiment_studio.spatial_demo_only" ),
                        QStringLiteral( "dock demo summarizer handles synthetic:// paths only; "
                                         "production uses GdalRasterDifferenceSummarizer" ) ) );
    }
};

} // namespace

ExperimentStudioDock::ExperimentStudioDock( QWidget *parent )
    : QgsDockWidget( tr( "Experiment Exploration Studio" ), parent )
{
    setObjectName( QStringLiteral( "rsExperimentExplorationStudioDock" ) );
    m_session.policy = defaultResourcePolicy();
    m_session.activeTab = QStringLiteral( "designer" );

    auto *root = new QWidget( this );
    auto *rootLayout = new QVBoxLayout( root );
    m_tabs = new QTabWidget( root );
    rootLayout->addWidget( m_tabs );
    setWidget( root );

    // ── A Designer ──
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout( page );
        m_algorithmEdit = new QLineEdit( QStringLiteral( "rs:threshold_raster" ) );
        m_strategyCombo = new QComboBox;
        m_strategyCombo->addItem( QStringLiteral( "one_at_a_time" ),
                                  static_cast<int>( SamplingStrategy::OneAtATime ) );
        m_strategyCombo->addItem( QStringLiteral( "grid" ),
                                  static_cast<int>( SamplingStrategy::Grid ) );
        m_strategyCombo->addItem( QStringLiteral( "latin_hypercube" ),
                                  static_cast<int>( SamplingStrategy::LatinHypercube ) );
        m_paramPathEdit = new QLineEdit( QStringLiteral( "threshold" ) );
        m_minSpin = new QDoubleSpinBox;
        m_minSpin->setRange( -1e6, 1e6 );
        m_minSpin->setValue( 0.1 );
        m_maxSpin = new QDoubleSpinBox;
        m_maxSpin->setRange( -1e6, 1e6 );
        m_maxSpin->setValue( 0.9 );
        m_stepsSpin = new QSpinBox;
        m_stepsSpin->setRange( 2, 1000 );
        m_stepsSpin->setValue( 5 );
        m_maxRunsSpin = new QSpinBox;
        m_maxRunsSpin->setRange( 1, 1000 );
        m_maxRunsSpin->setValue( 20 );
        m_replicatesSpin = new QSpinBox;
        m_replicatesSpin->setRange( 1, 32 );
        m_replicatesSpin->setValue( 1 );
        m_metricEdit = new QLineEdit( QStringLiteral( "maskedPercent" ) );
        m_designerLog = new QPlainTextEdit;
        m_designerLog->setReadOnly( true );
        auto *validateBtn = new QPushButton( tr( "Validate StudySpec" ) );
        auto *demoBtn = new QPushButton( tr( "Load NDVI/threshold demo" ) );
        form->addRow( tr( "Algorithm" ), m_algorithmEdit );
        form->addRow( tr( "Strategy" ), m_strategyCombo );
        form->addRow( tr( "Parameter" ), m_paramPathEdit );
        form->addRow( tr( "Min" ), m_minSpin );
        form->addRow( tr( "Max" ), m_maxSpin );
        form->addRow( tr( "Steps" ), m_stepsSpin );
        form->addRow( tr( "Max runs" ), m_maxRunsSpin );
        form->addRow( tr( "Seed replicates" ), m_replicatesSpin );
        form->addRow( tr( "Metric" ), m_metricEdit );
        form->addRow( validateBtn );
        form->addRow( demoBtn );
        form->addRow( m_designerLog );
        connect( validateBtn, &QPushButton::clicked, this, &ExperimentStudioDock::validateDesigner );
        connect( demoBtn, &QPushButton::clicked, this, &ExperimentStudioDock::loadDemoThresholdStudy );
        m_tabs->addTab( page, tr( "A Study Designer" ) );
    }

    // ── B Matrix ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        auto *row = new QHBoxLayout;
        m_filterEdit = new QLineEdit;
        m_filterEdit->setPlaceholderText( tr( "Filter point id…" ) );
        auto *applyFilter = new QPushButton( tr( "Filter" ) );
        auto *synth = new QPushButton( tr( "Synthetic matrix" ) );
        auto *cancelBtn = new QPushButton( tr( "Cancel (TaskCenter)" ) );
        row->addWidget( m_filterEdit );
        row->addWidget( applyFilter );
        row->addWidget( synth );
        row->addWidget( cancelBtn );
        layout->addLayout( row );
        m_matrixStatus = new QLabel;
        layout->addWidget( m_matrixStatus );
        m_matrixTable = new QTableWidget( 0, 6 );
        m_matrixTable->setHorizontalHeaderLabels(
            { tr( "Point" ), tr( "Rep" ), tr( "Run" ), tr( "Status" ), tr( "Metric" ),
              tr( "Error" ) } );
        m_matrixTable->horizontalHeader()->setStretchLastSection( true );
        m_matrixTable->setSelectionBehavior( QAbstractItemView::SelectRows );
        m_matrixTable->setSelectionMode( QAbstractItemView::ExtendedSelection );
        layout->addWidget( m_matrixTable );
        connect( applyFilter, &QPushButton::clicked, this, &ExperimentStudioDock::rebuildMatrixTable );
        connect( synth, &QPushButton::clicked, this, &ExperimentStudioDock::applySyntheticRunMatrix );
        connect( cancelBtn, &QPushButton::clicked, this,
                 &ExperimentStudioDock::cancelStudyPlaceholder );
        m_tabs->addTab( page, tr( "B Run Matrix" ) );
    }

    // ── C Spatial ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Compare two selected / demo pair" ) );
        m_spatialLog = new QPlainTextEdit;
        m_spatialLog->setReadOnly( true );
        layout->addWidget( btn );
        layout->addWidget( m_spatialLog );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::compareSelectedSpatial );
        m_tabs->addTab( page, tr( "C Spatial" ) );
    }

    // ── D Sensitivity ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Refresh sensitivity" ) );
        m_chart = new SensitivityChartWidget;
        m_sensitivityLog = new QPlainTextEdit;
        m_sensitivityLog->setReadOnly( true );
        layout->addWidget( btn );
        layout->addWidget( m_chart, 2 );
        layout->addWidget( m_sensitivityLog, 1 );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::refreshSensitivity );
        m_tabs->addTab( page, tr( "D Sensitivity" ) );
    }

    // ── E Fault ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        m_faultPrediction = new QLineEdit;
        m_faultPrediction->setPlaceholderText( tr( "Student diagnosis prediction…" ) );
        auto *btn = new QPushButton( tr( "Predict → Run sandbox → Diagnose" ) );
        m_faultLog = new QPlainTextEdit;
        m_faultLog->setReadOnly( true );
        layout->addWidget( m_faultPrediction );
        layout->addWidget( btn );
        layout->addWidget( m_faultLog );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::runFaultTeachingDemo );
        m_tabs->addTab( page, tr( "E Fault Teaching" ) );
    }

    // ── F Divergence ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Load first-divergence demo report" ) );
        m_divergenceLog = new QPlainTextEdit;
        m_divergenceLog->setReadOnly( true );
        layout->addWidget( btn );
        layout->addWidget( m_divergenceLog );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::loadFirstDivergenceDemo );
        m_tabs->addTab( page, tr( "F First Divergence" ) );
    }

    // ── G Export ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Export JSON + CSV bundle" ) );
        m_exportLog = new QPlainTextEdit;
        m_exportLog->setReadOnly( true );
        layout->addWidget( btn );
        layout->addWidget( m_exportLog );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::exportBundle );
        m_tabs->addTab( page, tr( "G Export" ) );
    }
}

QJsonObject ExperimentStudioDock::makeDemoOperatorSchema() const
{
    QJsonObject threshold;
    threshold.insert( QStringLiteral( "type" ), QStringLiteral( "number" ) );
    threshold.insert( QStringLiteral( "minimum" ), 0.0 );
    threshold.insert( QStringLiteral( "maximum" ), 1.0 );
    threshold.insert( QStringLiteral( "default" ), 0.3 );
    threshold.insert( QStringLiteral( "description" ), QStringLiteral( "NDVI threshold" ) );
    QJsonObject input;
    input.insert( QStringLiteral( "type" ), QStringLiteral( "string" ) );
    input.insert( QStringLiteral( "description" ), QStringLiteral( "input raster" ) );
    QJsonObject props;
    props.insert( QStringLiteral( "threshold" ), threshold );
    props.insert( QStringLiteral( "input" ), input );
    QJsonObject root;
    root.insert( QStringLiteral( "properties" ), props );
    return root;
}

QJsonObject ExperimentStudioDock::makeSyntheticStudyReport( int pointCount ) const
{
    StudyReport report;
    report.studyId = m_session.studyId.isEmpty() ? QStringLiteral( "studio-demo" ) : m_session.studyId;
    report.experimentId = m_session.experimentId.isEmpty() ? QStringLiteral( "exp-studio-demo" )
                                                           : m_session.experimentId;
    report.algorithmId = m_algorithmEdit->text();
    report.strategy = m_strategyCombo->currentText();
    SensitivityCurve curve;
    curve.parameterPath = m_paramPathEdit->text();
    curve.metricName = m_metricEdit->text();
    curve.trend = QStringLiteral( "increasing" );
    for ( int i = 0; i < pointCount; ++i )
    {
        StudyRunRow row;
        row.pointId = QStringLiteral( "p%1" ).arg( i, 4, 10, QLatin1Char( '0' ) );
        row.replicateIndex = 0;
        row.runId = QStringLiteral( "run-%1" ).arg( i );
        row.status = ( i == pointCount - 1 && pointCount > 2 ) ? QStringLiteral( "failed" )
                                                               : QStringLiteral( "recorded" );
        if ( row.status == QStringLiteral( "failed" ) )
        {
            row.errorSummary = QStringLiteral( "study.run_failed: demo failure" );
            report.failedCount++;
        }
        else
        {
            report.recordedCount++;
        }
        const double t =
            m_minSpin->value()
            + ( m_maxSpin->value() - m_minSpin->value() ) * ( pointCount == 1 ? 0.0 : double( i ) / ( pointCount - 1 ) );
        row.parameterAssignments.insert( m_paramPathEdit->text(), QString::number( t ) );
        row.metrics.insert( m_metricEdit->text(), t * 100.0 );
        row.outputAssetPath = QStringLiteral( "synthetic://out/%1.tif" ).arg( row.pointId );
        row.seed = 42;
        report.runTable.append( row );

        CurvePoint cp;
        cp.dimensionValue = t;
        cp.runCount = 1;
        cp.mean = t * 100.0;
        cp.min = cp.mean;
        cp.max = cp.mean;
        curve.points.append( cp );
    }
    report.curves.append( curve );
    report.specJson.insert( QStringLiteral( "budget" ),
                            QJsonObject{ { QStringLiteral( "seed_replicates" ),
                                           m_replicatesSpin->value() } } );
    return report.toJson();
}

void ExperimentStudioDock::loadDemoThresholdStudy()
{
    m_algorithmEdit->setText( QStringLiteral( "rs:threshold_raster" ) );
    m_paramPathEdit->setText( QStringLiteral( "threshold" ) );
    m_minSpin->setValue( 0.1 );
    m_maxSpin->setValue( 0.9 );
    m_stepsSpin->setValue( 5 );
    m_maxRunsSpin->setValue( 20 );
    m_metricEdit->setText( QStringLiteral( "maskedPercent" ) );
    m_session.studyId = QStringLiteral( "ndvi-threshold-studio" );
    m_session.experimentId = QStringLiteral( "exp-ndvi-studio" );
    m_session.algorithmId = m_algorithmEdit->text();
    validateDesigner();
    applySyntheticRunMatrix();
    refreshSensitivity();
    m_tabs->setCurrentIndex( 0 );
}

void ExperimentStudioDock::validateDesigner()
{
    ParameterDimension dim;
    dim.parameterPath = m_paramPathEdit->text();
    dim.minValue = m_minSpin->value();
    dim.maxValue = m_maxSpin->value();
    dim.stepCount = m_stepsSpin->value();
    StudyBudget budget;
    budget.maxRuns = m_maxRunsSpin->value();
    budget.maxInFlight = 2;
    budget.perRunTimeoutMs = 600000;
    budget.seedReplicates = m_replicatesSpin->value();
    budget.seed = 42;
    const auto strategy =
        static_cast<SamplingStrategy>( m_strategyCombo->currentData().toInt() );
    const auto vm = buildStudyDesignerViewModel(
        m_algorithmEdit->text(), makeDemoOperatorSchema(),
        QJsonObject{ { QStringLiteral( "input" ), QStringLiteral( "/data/ndvi.tif" ) } }, strategy,
        { dim }, budget, QStringList{ m_metricEdit->text() },
        m_session.studyId.isEmpty() ? QStringLiteral( "studio-draft" ) : m_session.studyId,
        m_session.experimentId.isEmpty() ? QStringLiteral( "exp-studio" ) : m_session.experimentId,
        m_session.policy, QStringLiteral( "bit_exact" ), true );
    if ( !vm )
    {
        m_designerLog->setPlainText( tr( "Designer failed" ) );
        return;
    }
    m_lastDesignerVm = vm.value().toJson();
    m_session.designerDraft = m_lastDesignerVm;
    m_designerLog->setPlainText(
        QString::fromUtf8( QJsonDocument( m_lastDesignerVm ).toJson( QJsonDocument::Indented ) ) );
}

void ExperimentStudioDock::applySyntheticRunMatrix()
{
    const int n = qBound( 2, m_stepsSpin->value(), 1000 );
    m_lastStudyReport = makeSyntheticStudyReport( n );
    m_session.lastStudyReport = m_lastStudyReport;
    rebuildMatrixTable();
    m_tabs->setCurrentIndex( 1 );
}

void ExperimentStudioDock::rebuildMatrixTable()
{
    const auto report = StudyReport::fromJson( m_lastStudyReport );
    if ( !report )
    {
        m_matrixStatus->setText( tr( "No report loaded" ) );
        return;
    }
    RunMatrixFilter filter;
    filter.pointIdContains = m_filterEdit->text().trimmed();
    const RunMatrixViewModel vm = projectRunMatrix( report.value(), filter );
    m_lastMatrixVm = vm.toJson();
    m_matrixStatus->setText(
        tr( "points=%1 recorded=%2 failed=%3 cancelled=%4 missing=%5" )
            .arg( vm.totalPoints )
            .arg( vm.recordedCount )
            .arg( vm.failedCount )
            .arg( vm.cancelledCount )
            .arg( vm.missingCount ) );
    m_matrixTable->setRowCount( 0 );
    m_matrixTable->setRowCount( vm.rows.size() );
    for ( int i = 0; i < vm.rows.size(); ++i )
    {
        const auto &r = vm.rows[i];
        m_matrixTable->setItem( i, 0, new QTableWidgetItem( r.pointId ) );
        m_matrixTable->setItem( i, 1, new QTableWidgetItem( QString::number( r.replicateIndex ) ) );
        m_matrixTable->setItem( i, 2, new QTableWidgetItem( r.runId ) );
        m_matrixTable->setItem( i, 3, new QTableWidgetItem( r.status ) );
        QString metric;
        if ( !r.metrics.isEmpty() )
        {
            const QString key = m_metricEdit->text();
            metric = r.metrics.contains( key ) ? QString::number( r.metrics.value( key ).toDouble() )
                                               : QString::number( r.metrics.begin().value().toDouble() );
        }
        m_matrixTable->setItem( i, 4, new QTableWidgetItem( metric ) );
        m_matrixTable->setItem( i, 5, new QTableWidgetItem( r.errorSummary ) );
    }
}

void ExperimentStudioDock::cancelStudyPlaceholder()
{
    // UI signals cancel intent; execution cancel propagates via StudyRunner /
    // TaskCenter — Studio owns no private thread pool.
    m_matrixStatus->setText(
        tr( "Cancel requested — propagates via StudyRunner → TaskCenter (no private pool)" ) );
}

void ExperimentStudioDock::compareSelectedSpatial()
{
    QString left = QStringLiteral( "synthetic://out/p0000.tif" );
    QString right = QStringLiteral( "synthetic://out/p0001.tif" );
    QString leftId = QStringLiteral( "p0000" );
    QString rightId = QStringLiteral( "p0001" );
    const auto selected = m_matrixTable->selectionModel()->selectedRows();
    if ( selected.size() >= 2 )
    {
        leftId = m_matrixTable->item( selected[0].row(), 0 )->text();
        rightId = m_matrixTable->item( selected[1].row(), 0 )->text();
        left = QStringLiteral( "synthetic://out/%1.tif" ).arg( leftId );
        right = QStringLiteral( "synthetic://out/%1.tif" ).arg( rightId );
    }
    BufferSummarizer summarizer;
    const auto vm =
        compareSpatialOutputs( leftId, rightId, left, right, summarizer, SpatialCompareMode::DiffSummary,
                               m_session.policy );
    if ( !vm )
    {
        m_spatialLog->setPlainText( tr( "compare failed" ) );
        return;
    }
    m_lastSpatialVm = vm.value().toJson();
    m_spatialLog->setPlainText(
        QString::fromUtf8( QJsonDocument( m_lastSpatialVm ).toJson( QJsonDocument::Indented ) ) );

    // Also demonstrate typed grid-mismatch refusal path
    const auto mismatch =
        compareSpatialOutputs( leftId, QStringLiteral( "mismatch" ), left,
                               QStringLiteral( "synthetic://mismatch.tif" ), summarizer );
    if ( mismatch )
    {
        m_spatialLog->appendPlainText( QStringLiteral( "\n--- mismatch demo ---\n" ) );
        m_spatialLog->appendPlainText( QString::fromUtf8(
            QJsonDocument( mismatch.value().toJson() ).toJson( QJsonDocument::Indented ) ) );
    }
}

void ExperimentStudioDock::refreshSensitivity()
{
    if ( m_lastStudyReport.isEmpty() )
        applySyntheticRunMatrix();
    const auto report = StudyReport::fromJson( m_lastStudyReport );
    if ( !report )
    {
        m_sensitivityLog->setPlainText( tr( "No report" ) );
        return;
    }
    const SensitivityViewModel vm = projectSensitivity( report.value() );
    m_lastSensitivityVm = vm.toJson();
    m_sensitivityLog->setPlainText(
        QString::fromUtf8( QJsonDocument( m_lastSensitivityVm ).toJson( QJsonDocument::Indented ) ) );
    rebuildChart();
}

void ExperimentStudioDock::rebuildChart()
{
    QVector<ChartSeriesPoint> pts;
    const QJsonArray curves = m_lastSensitivityVm.value( QStringLiteral( "curves" ) ).toArray();
    QString title = tr( "Sensitivity" );
    if ( !curves.isEmpty() )
    {
        const QJsonObject c = curves.first().toObject();
        title = c.value( QStringLiteral( "parameter_path" ) ).toString() + QStringLiteral( " → " )
                + c.value( QStringLiteral( "metric_name" ) ).toString();
        const QJsonArray points = c.value( QStringLiteral( "points" ) ).toArray();
        for ( const QJsonValue &v : points )
        {
            const QJsonObject p = v.toObject();
            ChartSeriesPoint cp;
            cp.x = p.value( QStringLiteral( "dimension_value" ) ).toDouble();
            cp.y = p.value( QStringLiteral( "mean" ) ).toDouble();
            cp.yMin = p.value( QStringLiteral( "min" ) ).toDouble( cp.y );
            cp.yMax = p.value( QStringLiteral( "max" ) ).toDouble( cp.y );
            pts.append( cp );
        }
    }
    m_chart->setSeries( title, pts, false );
}

void ExperimentStudioDock::runFaultTeachingDemo()
{
    QJsonObject scenario;
    scenario.insert( QStringLiteral( "scenario_id" ), QStringLiteral( "fault.demo.all_nodata" ) );
    scenario.insert( QStringLiteral( "title" ), QStringLiteral( "All NoData teaching fault" ) );
    scenario.insert( QStringLiteral( "learning_objective" ),
                     QStringLiteral( "Recognize all-nodata scientific fault" ) );
    scenario.insert( QStringLiteral( "expected_diagnosis_signature" ),
                     QStringLiteral( "all_nodata" ) );
    auto prior = projectFaultScenarioPredict( scenario );
    const QString prediction = m_faultPrediction->text().trimmed().isEmpty()
                                   ? QStringLiteral( "all_nodata" )
                                   : m_faultPrediction->text().trimmed();
    const QString originalFp = QStringLiteral( "sha256:original-demo" );
    const auto vm = projectFaultDiagnosis( prior, prediction, QStringLiteral( "all_nodata" ),
                                           QJsonObject{ { QStringLiteral( "observable" ),
                                                          QStringLiteral( "all_nodata" ) } },
                                           QStringLiteral( "/tmp/fault-sandbox-demo" ), originalFp,
                                           originalFp );
    m_lastFaultVm = vm.toJson();
    m_session.faultScenarioId = vm.scenarioId;
    m_faultLog->setPlainText(
        QString::fromUtf8( QJsonDocument( m_lastFaultVm ).toJson( QJsonDocument::Indented ) ) );
}

void ExperimentStudioDock::loadFirstDivergenceDemo()
{
    QJsonObject first;
    first.insert( QStringLiteral( "kind" ), QStringLiteral( "ParameterDivergence" ) );
    first.insert( QStringLiteral( "confidence" ), QStringLiteral( "high" ) );
    first.insert( QStringLiteral( "reference_step_id" ), QStringLiteral( "step-threshold" ) );
    first.insert( QStringLiteral( "student_step_id" ), QStringLiteral( "step-threshold" ) );
    first.insert( QStringLiteral( "evidence" ),
                  QJsonArray{ QStringLiteral( "param:threshold ref=0.3 student=0.5" ) } );
    first.insert( QStringLiteral( "missing_evidence" ),
                  QJsonArray{ QStringLiteral( "upstream_digest" ) } );
    QJsonObject report;
    report.insert( QStringLiteral( "reference_run_id" ), QStringLiteral( "run-ref" ) );
    report.insert( QStringLiteral( "student_run_id" ), QStringLiteral( "run-student" ) );
    report.insert( QStringLiteral( "verdict" ), QStringLiteral( "incomplete" ) );
    report.insert( QStringLiteral( "has_first_divergence" ), true );
    report.insert( QStringLiteral( "first_divergence" ), first );
    report.insert( QStringLiteral( "evidence_gaps" ),
                   QJsonArray{ QStringLiteral( "upstream_digest" ) } );
    const FirstDivergenceViewModel vm = projectFirstDivergence( report );
    m_lastDivergenceVm = vm.toJson();
    m_session.referenceRunId = vm.referenceRunId;
    m_session.studentRunId = vm.studentRunId;
    m_divergenceLog->setPlainText(
        QString::fromUtf8( QJsonDocument( m_lastDivergenceVm ).toJson( QJsonDocument::Indented ) ) );
}

void ExperimentStudioDock::exportBundle()
{
    if ( m_lastStudyReport.isEmpty() )
        applySyntheticRunMatrix();
    StudioExportBundle bundle;
    bundle.studyId = m_session.studyId;
    bundle.studyReport = m_lastStudyReport;
    bundle.faultTeaching = m_lastFaultVm;
    bundle.firstDivergence = m_lastDivergenceVm;
    bundle.designer = m_lastDesignerVm;
    bundle.runMatrix = m_lastMatrixVm;
    const QJsonArray table = m_lastStudyReport.value( QStringLiteral( "run_table" ) ).toArray();
    for ( const QJsonValue &v : table )
    {
        const QString runId = v.toObject().value( QStringLiteral( "run_id" ) ).toString();
        if ( !runId.isEmpty() )
            bundle.runIds.append( runId );
    }
    bundle.csvRunTable = studyRunTableToCsv( m_lastStudyReport );
    const QString path = QFileDialog::getSaveFileName(
        this, tr( "Export Studio bundle" ), QStringLiteral( "studio-export.json" ),
        tr( "JSON (*.json)" ) );
    if ( path.isEmpty() )
        return;
    const auto written = writeStudioExportJson( bundle, path );
    if ( !written )
    {
        m_exportLog->setPlainText( tr( "Export failed" ) );
        return;
    }
    m_session.lastExportPath = path;
    m_exportLog->setPlainText(
        tr( "Wrote %1\nCSV rows embedded; offline reload via StudioExportBundle::fromJson" )
            .arg( path ) );
}

} // namespace sicnu::app
