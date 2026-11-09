#include "experiment_studio/experiment_studio_dock.h"
#include "experiment_studio/sensitivity_chart_widget.h"

#include "dataset/dataset_store.h"
#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "experiment/reproduction_bundle_import.h"
#include "experiment_studio/fault_teaching_projection.h"
#include "experiment_studio/first_divergence_projection.h"
#include "experiment_studio/live/studio_live.h"
#include "experiment_studio/run_matrix_projection.h"
#include "experiment_studio/sensitivity_projection.h"
#include "experiment_studio/spatial_compare_projection.h"
#include "experiment_studio/studio_export.h"
#include "experiment_studio/study_designer.h"
#include "study/bridge/study_execution_plane.h"
#include "study/study_export.h"
#include "study/study_spatial.h"
#include "operators/framework/rs_operator_registry.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <atomic>

using sicnu::app::experiment_studio::ChartSeriesPoint;
using sicnu::app::experiment_studio::SensitivityChartWidget;
using namespace sicnu::experiment_studio;
using namespace sicnu::experiment_studio::live;
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
                                        "the live path uses GdalRasterDifferenceSummarizer" ) ) );
    }
};

/// Session tab identity order — ONE definition shared by the tab-change
/// writer and the session restore reader (index shifts must break both or
/// neither).
const QStringList kStudioTabIds = {
    QStringLiteral( "designer" ),     QStringLiteral( "matrix" ),
    QStringLiteral( "spatial" ),      QStringLiteral( "sensitivity" ),
    QStringLiteral( "fault" ),        QStringLiteral( "divergence" ),
    QStringLiteral( "export" ),
};

} // namespace

ExperimentStudioDock::ExperimentStudioDock( QWidget *parent )
    : QgsDockWidget( tr( "Experiment Exploration Studio" ), parent )
{
    setObjectName( QStringLiteral( "rsExperimentExplorationStudioDock" ) );
    m_session.policy = defaultResourcePolicy();
    m_session.activeTab = QStringLiteral( "designer" );
    m_liveDatasets = std::make_unique<sicnu::dataset::DatasetStore>();

    auto *root = new QWidget( this );
    auto *rootLayout = new QVBoxLayout( root );
    m_tabs = new QTabWidget( root );
    rootLayout->addWidget( m_tabs );
    setWidget( root );

    const bool demoEnabled = qgetenv( "SICNU_UI_DEMO_MODE" ) == QByteArrayLiteral( "1" );

    // One scrolling contract for every studio page (#1822): long schema
    // forms, tables and logs must stay reachable at 200% text scale or in a
    // short dock. Stable object names keep QA locators working.
    auto wrapScrollable = []( QWidget *page, const QString &objectName ) -> QScrollArea * {
        auto *scroll = new QScrollArea;
        scroll->setObjectName( objectName );
        scroll->setWidgetResizable( true );
        scroll->setFrameShape( QFrame::NoFrame );
        scroll->setWidget( page );
        return scroll;
    };

    // ── Study designer ──
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout( page );
        m_algorithmEdit = new QLineEdit( QStringLiteral( "rs:threshold_raster" ) );
        m_algorithmEdit->setObjectName( QStringLiteral( "rsStudioAlgorithmEdit" ) );
        m_algorithmEdit->setReadOnly( true );
        auto *browseAlgorithmBtn = new QPushButton( tr( "Browse…" ) );
        auto *algorithmRow = new QHBoxLayout;
        algorithmRow->addWidget( m_algorithmEdit, 1 );
        algorithmRow->addWidget( browseAlgorithmBtn );
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
        m_inputRasterEdit = new QLineEdit;
        m_inputRasterEdit->setPlaceholderText( tr( "Input raster (required for a real study)" ) );
        m_designerSummary = new QLabel( tr( "Configure a study, then validate the design." ) );
        m_designerSummary->setObjectName( QStringLiteral( "rsStudioDesignerSummary" ) );
        m_designerSummary->setWordWrap( true );
        m_designerLog = new QPlainTextEdit;
        m_designerLog->setReadOnly( true );
        m_designerLog->setVisible( false );
        auto *detailsToggle = new QPushButton( tr( "Technical details (JSON)" ) );
        detailsToggle->setCheckable( true );
        auto *validateBtn = new QPushButton( tr( "Validate Study Design" ) );
        auto *demoBtn = new QPushButton( tr( "Load NDVI/threshold demo" ) );
        demoBtn->setVisible( demoEnabled );
        m_openStoreBtn = new QPushButton( tr( "Open experiment store…" ) );
        m_importBundleBtn = new QPushButton( tr( "Import Reproduction Bundle…" ) );
        m_importBundleBtn->setObjectName( QStringLiteral( "rsStudioImportBundleBtn" ) );
        m_importBundleBtn->setEnabled( false );
        m_runLiveBtn = new QPushButton( tr( "Run Study" ) );
        m_runLiveBtn->setObjectName( QStringLiteral( "rsStudioRunLiveBtn" ) );
        m_runLiveBtn->setEnabled( false );
        form->addRow( tr( "Algorithm" ), algorithmRow );
        form->addRow( tr( "Strategy" ), m_strategyCombo );
        form->addRow( tr( "Parameter" ), m_paramPathEdit );
        form->addRow( tr( "Min" ), m_minSpin );
        form->addRow( tr( "Max" ), m_maxSpin );
        form->addRow( tr( "Steps" ), m_stepsSpin );
        form->addRow( tr( "Max runs" ), m_maxRunsSpin );
        form->addRow( tr( "Seed replicates" ), m_replicatesSpin );
        form->addRow( tr( "Metric" ), m_metricEdit );
        form->addRow( tr( "Input raster" ), m_inputRasterEdit );
        form->addRow( validateBtn );
        form->addRow( demoBtn );
        form->addRow( m_openStoreBtn );
        form->addRow( m_importBundleBtn );
        form->addRow( m_runLiveBtn );
        form->addRow( m_designerSummary );
        form->addRow( detailsToggle );
        form->addRow( m_designerLog );
        connect( browseAlgorithmBtn, &QPushButton::clicked, this, [this] {
            QStringList ids;
            for ( const std::string &id : sicnu::operators::RSOperatorRegistry::instance().operatorNames() )
                ids.append( QString::fromStdString( id ) );
            ids.sort();
            bool accepted = false;
            const QString selected = QInputDialog::getItem(
                this, tr( "Select Operator" ), tr( "Algorithm" ), ids,
                std::max<qsizetype>( 0, ids.indexOf( m_algorithmEdit->text() ) ), false, &accepted );
            if ( accepted && !selected.isEmpty() )
                m_algorithmEdit->setText( selected );
        } );
        connect( detailsToggle, &QPushButton::toggled, m_designerLog, &QWidget::setVisible );
        connect( validateBtn, &QPushButton::clicked, this, &ExperimentStudioDock::validateDesigner );
        connect( demoBtn, &QPushButton::clicked, this, &ExperimentStudioDock::loadDemoThresholdStudy );
        connect( m_openStoreBtn, &QPushButton::clicked, this,
                 &ExperimentStudioDock::openLiveStore );
        connect( m_importBundleBtn, &QPushButton::clicked, this,
                 &ExperimentStudioDock::importReproductionBundleDialog );
        connect( m_runLiveBtn, &QPushButton::clicked, this, &ExperimentStudioDock::runLiveStudy );
        m_tabs->addTab( wrapScrollable( page, QStringLiteral( "rsStudioDesignerScroll" ) ),
                        tr( "Study Designer" ) );
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
        synth->setVisible( demoEnabled );
        auto *cancelBtn = new QPushButton( tr( "Cancel Study" ) );
        row->addWidget( m_filterEdit );
        row->addWidget( applyFilter );
        row->addWidget( synth );
        row->addWidget( cancelBtn );
        layout->addLayout( row );
        m_matrixStatus = new QLabel;
        m_matrixStatus->setObjectName( QStringLiteral( "rsStudioMatrixStatus" ) );
        layout->addWidget( m_matrixStatus );
        m_matrixTable = new QTableWidget( 0, 6 );
        m_matrixTable->setObjectName( QStringLiteral( "rsStudioMatrixTable" ) );
        m_matrixTable->setHorizontalHeaderLabels(
            { tr( "Point" ), tr( "Rep" ), tr( "Run" ), tr( "Status" ), tr( "Metric" ),
              tr( "Error" ) } );
        m_matrixTable->horizontalHeader()->setStretchLastSection( true );
        m_matrixTable->setSelectionBehavior( QAbstractItemView::SelectRows );
        m_matrixTable->setSelectionMode( QAbstractItemView::ExtendedSelection );
        layout->addWidget( m_matrixTable );
        connect( applyFilter, &QPushButton::clicked, this, &ExperimentStudioDock::rebuildMatrixTable );
        connect( synth, &QPushButton::clicked, this, &ExperimentStudioDock::applySyntheticRunMatrix );
        connect( cancelBtn, &QPushButton::clicked, this, &ExperimentStudioDock::cancelLiveStudy );
        m_tabs->addTab( wrapScrollable( page, QStringLiteral( "rsStudioMatrixScroll" ) ),
                        tr( "Run Matrix" ) );
    }

    // ── Spatial comparison ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Compare Two Selected Runs" ) );
        m_spatialLog = new QPlainTextEdit;
        m_spatialLog->setReadOnly( true );
        layout->addWidget( btn );
        layout->addWidget( m_spatialLog );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::compareSelectedSpatial );
        m_tabs->addTab( wrapScrollable( page, QStringLiteral( "rsStudioSpatialScroll" ) ),
                        tr( "Spatial Comparison" ) );
    }

    // ── Sensitivity ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Refresh Sensitivity" ) );
        m_chart = new SensitivityChartWidget;
        m_sensitivityLog = new QPlainTextEdit;
        m_sensitivityLog->setReadOnly( true );
        layout->addWidget( btn );
        layout->addWidget( m_chart, 2 );
        layout->addWidget( m_sensitivityLog, 1 );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::refreshSensitivity );
        m_tabs->addTab( wrapScrollable( page, QStringLiteral( "rsStudioSensitivityScroll" ) ),
                        tr( "Sensitivity" ) );
    }

    // ── Fault teaching ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        m_faultPrediction = new QLineEdit;
        m_faultPrediction->setPlaceholderText( tr( "Student diagnosis prediction…" ) );
        auto *btn = new QPushButton( tr( "Predict → Run Sandbox → Diagnose" ) );
        m_faultLog = new QPlainTextEdit;
        m_faultLog->setReadOnly( true );
        layout->addWidget( m_faultPrediction );
        layout->addWidget( btn );
        layout->addWidget( m_faultLog );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::runFaultTeachingDemo );
        m_tabs->addTab( wrapScrollable( page, QStringLiteral( "rsStudioFaultScroll" ) ),
                        tr( "Fault Teaching" ) );
    }

    // ── First divergence ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Localize First Divergence" ) );
        m_divergenceLog = new QPlainTextEdit;
        m_divergenceLog->setReadOnly( true );
        layout->addWidget( btn );
        layout->addWidget( m_divergenceLog );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::loadFirstDivergenceDemo );
        m_tabs->addTab( wrapScrollable( page, QStringLiteral( "rsStudioDivergenceScroll" ) ),
                        tr( "First Divergence" ) );
    }

    // ── Export ──
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Export JSON + CSV bundle" ) );
        m_exportLog = new QPlainTextEdit;
        m_exportLog->setReadOnly( true );
        layout->addWidget( btn );
        layout->addWidget( m_exportLog );
        connect( btn, &QPushButton::clicked, this, &ExperimentStudioDock::exportBundle );
        m_tabs->addTab( wrapScrollable( page, QStringLiteral( "rsStudioExportScroll" ) ),
                        tr( "Export" ) );
    }

    // Tab identity follows the session (D4): navigation is part of the
    // persisted draft, so a restored session reopens where the user left.
    connect( m_tabs, &QTabWidget::currentChanged, this, [this]( int index ) {
        if ( index >= 0 && index < kStudioTabIds.size()
             && m_session.activeTab != kStudioTabIds[index] )
        {
            m_session.activeTab = kStudioTabIds[index];
            savePersistedSession();
        }
    } );

    restorePersistedSession();
}

QJsonObject ExperimentStudioDock::registryOperatorSchema( const QString &algorithmId,
                                                          QString *determinismGradeOut ) const
{
    if ( determinismGradeOut )
        *determinismGradeOut = QString();
    if ( algorithmId.isEmpty() )
        return {};
    const auto op = sicnu::operators::RSOperatorRegistry::instance().create(
        algorithmId.toStdString() );
    if ( !op )
        return {};
    if ( determinismGradeOut )
        *determinismGradeOut = QString::fromStdString( op->determinismGrade() );
    // jsoncpp → QByteArray → QJson round-trip: the registry speaks jsoncpp,
    // the designer projection speaks QJson. Serialize compact and reparse —
    // a schema that cannot survive its own serialization is not a schema.
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    writer["commentStyle"] = "None";
    const std::string bytes = Json::writeString( writer, op->schema() );
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString( bytes ), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
        return {};
    return doc.object();
}

sicnu::study::ParameterStudySpec ExperimentStudioDock::designerSpecFromFields(
    const QString &inputRaster ) const
{
    ParameterDimension dim;
    dim.parameterPath = m_paramPathEdit->text().trimmed();
    dim.minValue = m_minSpin->value();
    dim.maxValue = m_maxSpin->value();
    dim.stepCount = m_stepsSpin->value();
    StudyBudget budget;
    budget.maxRuns = m_maxRunsSpin->value();
    budget.maxInFlight = 2;
    // Teaching studies are small: a 60s per-point deadline bounds the
    // close-time join in ~ExperimentStudioDock (StudyRunner drains in-flight
    // points before the wait returns).
    budget.perRunTimeoutMs = 60000;
    budget.seedReplicates = m_replicatesSpin->value();
    budget.seed = 42;
    ParameterStudySpec spec;
    spec.studyId = m_session.studyId.isEmpty() ? QStringLiteral( "studio-live" )
                                               : m_session.studyId;
    spec.experimentId = m_session.experimentId.isEmpty() ? QStringLiteral( "exp-studio-live" )
                                                         : m_session.experimentId;
    spec.algorithmId = m_algorithmEdit->text().trimmed();
    if ( !inputRaster.isEmpty() )
        spec.baseParameters.insert( QStringLiteral( "input" ), inputRaster );
    spec.strategy = static_cast<SamplingStrategy>( m_strategyCombo->currentData().toInt() );
    spec.dimensions.append( dim );
    spec.budget = budget;
    spec.metricNames.append( m_metricEdit->text().trimmed() );
    return spec;
}

QStringList ExperimentStudioDock::validateDesignerSpec(
    const sicnu::study::ParameterStudySpec &spec, QString *determinismGradeOut,
    QJsonObject *vmJsonOut ) const
{
    if ( vmJsonOut )
        *vmJsonOut = QJsonObject{};
    QStringList issues;
    QString determinismGrade;
    const QJsonObject schema = registryOperatorSchema( spec.algorithmId, &determinismGrade );
    if ( determinismGradeOut )
        *determinismGradeOut = determinismGrade;
    if ( schema.isEmpty() )
        issues.append( QStringLiteral( "experiment_studio.algorithm_unknown:%1 is not in the operator registry" )
                           .arg( spec.algorithmId ) );
    // The same real-input gate the live run enforces: a design over a
    // missing input is not "valid pending details".
    const QString inputRaster = spec.baseParameters.value( QStringLiteral( "input" ) ).toString();
    if ( inputRaster.isEmpty() )
        issues.append( QStringLiteral(
            "experiment_studio.input_required: set an existing input raster before validating" ) );
    else if ( !QFile::exists( inputRaster ) )
        issues.append( QStringLiteral( "experiment_studio.input_missing:%1 does not exist" )
                           .arg( inputRaster ) );
    const auto vm = buildStudyDesignerViewModel(
        spec.algorithmId, schema, spec.baseParameters, spec.strategy, spec.dimensions,
        spec.budget, spec.metricNames, spec.studyId, spec.experimentId, m_session.policy,
        determinismGrade.isEmpty() ? QStringLiteral( "unknown" ) : determinismGrade,
        spec.spatialComparison );
    if ( vm )
    {
        issues += vm->draftIssues;
        issues += vm->suitabilityIssues;
        if ( vmJsonOut && issues.isEmpty() )
            *vmJsonOut = vm->toJson();
    }
    else
    {
        for ( const auto &d : vm.diagnostics() )
            issues.append( d.code + QLatin1Char( ':' ) + d.message );
    }
    return issues;
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
    // The design gate now requires a REAL input (D1); a demo that graded
    // itself against it would just paint a refusal banner next to an
    // explicitly synthetic matrix. The demo remains clearly labeled
    // synthetic; validation stays a separate, real act — and the demo
    // CLEARS any previously validated draft so the export bundle never
    // mixes a real design with synthetic rows.
    m_lastDesignerVm = QJsonObject{};
    m_session.designerDraft = QJsonObject{};
    applySyntheticRunMatrix();
    refreshSensitivity();
    m_tabs->setCurrentIndex( 0 );
}

void ExperimentStudioDock::validateDesigner()
{
    // D1 (R29-T04): validation consumes the REAL registry schema and the
    // REAL input field through the SAME normalized spec the live run
    // submits — the demo schema + fixed /data/ndvi.tif pair used to let a
    // "valid design" pass checks the run never shared.
    const ParameterStudySpec spec =
        designerSpecFromFields( m_inputRasterEdit->text().trimmed() );
    QJsonObject vmJson;
    const QStringList issues = validateDesignerSpec( spec, nullptr, &vmJson );
    if ( !issues.isEmpty() )
    {
        m_designerSummary->setText( tr( "The study design is invalid. Open technical details for diagnostics." ) );
        m_designerLog->setPlainText( issues.join( QLatin1Char( '\n' ) ) );
        // A refused design leaves no draft behind — in memory OR on disk:
        // the session and the export bundle must not carry (or resurrect on
        // restart) a stale "valid" spec from before the algorithm/input
        // changed.
        m_lastDesignerVm = QJsonObject{};
        m_session.designerDraft = QJsonObject{};
        savePersistedSession();
        return;
    }
    m_designerSummary->setText(
        tr( "Valid design: %1 using %2; at most %3 runs with metric %4." )
            .arg( m_algorithmEdit->text(), m_strategyCombo->currentText() )
            .arg( m_maxRunsSpin->value() )
            .arg( m_metricEdit->text() ) );
    m_lastDesignerVm = vmJson;
    m_session.designerDraft = m_lastDesignerVm;
    savePersistedSession();
    m_designerLog->setPlainText(
        QString::fromUtf8( QJsonDocument( m_lastDesignerVm ).toJson( QJsonDocument::Indented ) ) );
}

void ExperimentStudioDock::applySyntheticRunMatrix()
{
    // A live store means the export bundle would mix demo rows with real
    // capsule refs — refuse the demo instead of contaminating the bundle.
    if ( m_liveStore )
    {
        m_matrixStatus->setText(
            tr( "Demo report refused: a live experiment store is open "
                "(synthetic rows would contaminate live exports)" ) );
        return;
    }
    const int n = qBound( 2, m_stepsSpin->value(), 1000 );
    m_lastStudyReport = makeSyntheticStudyReport( n );
    // Export honesty: this report is a teaching scaffold derived from NO
    // recorded run. Carry the marker on the document itself so the exported
    // bundle cannot pass it off as recorded analysis; the report parser is
    // unknown-key tolerant, so the marker survives reloads.
    m_lastStudyReport.insert( QStringLiteral( "synthetic" ), true );
    m_lastStudyReport.insert(
        QStringLiteral( "synthetic_note" ),
        QStringLiteral( "studio demonstration document, not derived from"
                        " recorded runs" ) );
    m_session.lastStudyReport = m_lastStudyReport;
    savePersistedSession();
    rebuildMatrixTable();
    m_tabs->setCurrentIndex( 1 );
}

void ExperimentStudioDock::rebuildMatrixTable()
{
    const auto report = StudyReport::fromJson( m_lastStudyReport );
    if ( !report )
    {
        m_matrixStatus->setText( tr( "No report loaded" ) );
        m_matrixTable->clearContents();
        m_matrixTable->setRowCount( 0 );
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

    constexpr int kMaxMatrixDisplayRows = 500;
    const int totalRows = vm.rows.size();
    const int displayCount = std::min( totalRows, kMaxMatrixDisplayRows );

    const bool sortingWasEnabled = m_matrixTable->isSortingEnabled();
    m_matrixTable->setSortingEnabled( false );
    m_matrixTable->setUpdatesEnabled( false );
    m_matrixTable->clearContents();
    m_matrixTable->setRowCount( displayCount );
    for ( int i = 0; i < displayCount; ++i )
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
    m_matrixTable->setUpdatesEnabled( true );
    m_matrixTable->setSortingEnabled( sortingWasEnabled );
    if ( totalRows > kMaxMatrixDisplayRows && m_matrixStatus )
    {
        m_matrixStatus->setText( m_matrixStatus->text() +
                                 tr( " (preview limited to first %1 of %2 rows)" )
                                     .arg( kMaxMatrixDisplayRows )
                                     .arg( totalRows ) );
    }
}

ExperimentStudioDock::~ExperimentStudioDock()
{
    m_aliveFlag->store( false, std::memory_order_release );
    if ( m_liveCancel )
        m_liveCancel->store( true );

    // #1805 F18: the destructor used to JOIN the live worker here — up to
    // 60 s of frozen GUI while one long study point drained (the cancel
    // flag is only observed BETWEEN points). The worker's delivery now
    // rides a GUI-thread QPointer with qApp as the invoke context, so it
    // cannot touch freed dock memory after this destructor returns; a
    // detached supervisor joins the worker out-of-band instead.
    std::vector<RetiredStudyThread> toReap;
    {
        std::lock_guard<std::mutex> lock( m_retiredStudyMutex );
        toReap.swap( m_retiredStudyThreads );
    }
    if ( m_liveRunThread && m_liveRunThread->joinable() )
    {
        std::thread( [t = std::move( m_liveRunThread ),
                      reapers = std::move( toReap )]() mutable {
            if ( t && t->joinable() )
                t->join();
            for ( auto &r : reapers )
                if ( r.thread.joinable() )
                    r.thread.join();
        } ).detach();
    }
    else
    {
        // No live worker: retired reapers join already-drained threads
        // (bounded to microseconds) — safe to do inline.
        for ( auto &r : toReap )
            if ( r.thread.joinable() )
                r.thread.join();
    }
    m_liveRunThread.reset();
}

void ExperimentStudioDock::openLiveStore()
{
    if ( liveBusy() )
    {
        m_designerLog->setPlainText( tr( "study run in progress — wait or cancel first" ) );
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr( "Open experiment store" ), QString(),
        tr( "SQLite store (*.db *.sqlite *.sqlite3);;All files (*)" ) );
    if ( path.isEmpty() )
        return;
    openLiveStoreAtPath( path );
}

bool ExperimentStudioDock::openLiveStoreAtPath( const QString &path )
{
    if ( liveBusy() )
    {
        // The in-flight run's queued result must land on the store it ran
        // against — switching stores mid-run would let run A's callback
        // publish its report over store B's session state.
        m_designerLog->setPlainText( tr( "study run in progress — wait or cancel first" ) );
        return false;
    }
    auto store = std::make_shared<sicnu::experiment::ExperimentStore>();
    QString error;
    if ( !store->open( path, &error ) )
    {
        logTypedFailure( m_designerLog, QStringLiteral( "open store" ),
                         QStringLiteral( "experiment_studio.store_open_failed" ), error );
        return false;
    }
    m_liveStore = store;
    // Run refs from a previous store must not leak into the new one's
    // divergence/export flows (capsule.run-missing noise at best).
    m_session.referenceRunId.clear();
    m_session.studentRunId.clear();
    m_session.faultScenarioId.clear();
    m_lastStudyReport = QJsonObject{};
    m_session.lastStudyReport = QJsonObject{};
    rebuildMatrixTable();
    m_liveLedger = std::make_shared<sicnu::experiment::MatrixLedger>( *store );
    m_liveStudyOutputDir =
        QFileInfo( path ).absolutePath() + QStringLiteral( "/studio-study-outputs" );
    m_session.liveStorePath = path;
    m_designerSummary->setText(
        tr( "Experiment store open: %1. Set the input raster, then Run Study." ).arg( path ) );
    m_designerLog->setPlainText(
        tr( "Experiment store open: %1\nStudy outputs will be committed under %2\n"
            "Set the input raster, then Run Study." )
            .arg( path, m_liveStudyOutputDir ) );
    m_runLiveBtn->setEnabled( true );
    m_importBundleBtn->setEnabled( true );
    savePersistedSession();
    return true;
}

QString ExperimentStudioDock::studioSessionPath() const
{
    const QString root = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    QDir().mkpath( root + QStringLiteral( "/experiment_studio" ) );
    return root + QStringLiteral( "/experiment_studio/studio_session.json" );
}

void ExperimentStudioDock::savePersistedSession()
{
    QFile file( studioSessionPath() );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return; // best-effort persistence; the in-memory session stays truth
    file.write( QJsonDocument( m_session.toJson() ).toJson( QJsonDocument::Indented ) );
}

void ExperimentStudioDock::restorePersistedSession()
{
    QFile file( studioSessionPath() );
    if ( !file.exists() )
        return;
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        m_designerSummary->setText(
          tr( "A previous studio session exists but cannot be read; it was not restored." ) );
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
    file.close();
    if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
    {
        // Damaged document: explicit recovery note — never a silent fresh
        // start, never a partial adoption of the fields that happen to parse.
        m_designerSummary->setText(
          tr( "The saved studio session is damaged and was not restored. Design anew or "
              "re-open your experiment store." ) );
        return;
    }
    const sicnu::experiment_studio::StudioSessionState restored =
        sicnu::experiment_studio::StudioSessionState::fromJson( doc.object() );
    if ( !restored.ok )
    {
        m_designerSummary->setText(
          tr( "The saved studio session was written by another version and was not restored." ) );
        return;
    }
    m_session = restored;
    m_lastDesignerVm = m_session.designerDraft;
    m_lastStudyReport = m_session.lastStudyReport;

    // The normalized draft spec carries the last validated design: the
    // widgets follow it so the user re-opens WHAT they designed (D4). A
    // report without a draft still restores the recorded results below.
    const QJsonObject spec = m_session.designerDraft.value( QStringLiteral( "draft_spec" ) ).toObject();
    if ( !spec.isEmpty() )
    {
        m_algorithmEdit->setText( spec.value( QStringLiteral( "algorithm_id" ) ).toString() );
        m_inputRasterEdit->setText(
          spec.value( QStringLiteral( "base_parameters" ) ).toObject().value( QStringLiteral( "input" ) ).toString() );
        const QJsonArray dims = spec.value( QStringLiteral( "dimensions" ) ).toArray();
        if ( !dims.isEmpty() )
        {
            const QJsonObject dim = dims.first().toObject();
            m_paramPathEdit->setText( dim.value( QStringLiteral( "parameter_path" ) ).toString() );
            m_minSpin->setValue( dim.value( QStringLiteral( "min_value" ) ).toDouble() );
            m_maxSpin->setValue( dim.value( QStringLiteral( "max_value" ) ).toDouble() );
            m_stepsSpin->setValue(
              static_cast<int>( dim.value( QStringLiteral( "step_count" ) ).toDouble() ) );
        }
        const QString strategy = spec.value( QStringLiteral( "sampling" ) )
                                   .toObject()
                                   .value( QStringLiteral( "strategy" ) )
                                   .toString();
        // Core wire names vs designer combo texts: oat/lhs are spelled out
        // in the combo ("one_at_a_time"/"latin_hypercube").
        QString strategyText = strategy;
        if ( strategy == QStringLiteral( "oat" ) )
            strategyText = QStringLiteral( "one_at_a_time" );
        else if ( strategy == QStringLiteral( "lhs" ) )
            strategyText = QStringLiteral( "latin_hypercube" );
        const int strategyIndex = m_strategyCombo->findText( strategyText );
        if ( strategyIndex >= 0 )
            m_strategyCombo->setCurrentIndex( strategyIndex );
        const QJsonObject budget = spec.value( QStringLiteral( "budget" ) ).toObject();
        m_maxRunsSpin->setValue(
          static_cast<int>( budget.value( QStringLiteral( "max_runs" ) ).toDouble() ) );
        m_replicatesSpin->setValue(
          static_cast<int>( budget.value( QStringLiteral( "seed_replicates" ) ).toDouble() ) );
        const QJsonArray metrics = spec.value( QStringLiteral( "metrics" ) ).toArray();
        if ( !metrics.isEmpty() )
            m_metricEdit->setText( metrics.first().toString() );
    }

    const int tabIndex = kStudioTabIds.indexOf( m_session.activeTab );
    if ( tabIndex >= 0 && tabIndex != m_tabs->currentIndex() )
        m_tabs->setCurrentIndex( tabIndex );

    if ( !m_lastStudyReport.isEmpty() )
    {
        rebuildMatrixTable();
        refreshSensitivity();
    }
    m_designerSummary->setText(
      tr( "Previous studio session restored (study %1). Recorded results are evidence, "
          "not a live connection%2." )
        .arg( m_session.studyId.isEmpty() ? tr( "(draft)" ) : m_session.studyId,
              m_session.liveStorePath.isEmpty()
                ? QString()
                : QStringLiteral( "; last store: %1" ).arg( m_session.liveStorePath ) ) );
}

sicnu::experiment::ReproductionBundleImportReport
ExperimentStudioDock::importReproductionBundleAtPath( const QString &bundleDir,
                                                      const QString &targetExperimentId,
                                                      bool keepOriginalRunId, bool dryRun )
{
    sicnu::experiment::ReproductionBundleImportReport report;
    report.warnings.append( tr( "no live experiment store open" ) );
    if ( !m_liveStore )
        return report;
    if ( liveBusy() )
    {
        report.warnings.append( tr( "study run in progress — wait or cancel first" ) );
        return report;
    }
    report.warnings.clear();
    sicnu::experiment::ReproductionBundleImportOptions options;
    options.bundleDir = bundleDir;
    options.targetExperimentId = targetExperimentId;
    options.keepOriginalRunId = keepOriginalRunId;
    options.dryRun = dryRun;
    // No relocation by default: nothing is overwritten without an explicit
    // user choice; conflicts stay visible in the report (WE-30 contract).
    const sicnu::experiment::ReproductionBundleImporter importer( *m_liveStore );
    return importer.importRun( options );
}

void ExperimentStudioDock::importReproductionBundleDialog()
{
    if ( !m_liveStore || liveBusy() )
    {
        m_designerLog->setPlainText(
          tr( "Import needs an open, idle experiment store (open one first; "
              "a run in flight pins the store)." ) );
        return;
    }
    const QString bundleDir = QFileDialog::getExistingDirectory(
      this, tr( "Select reproduction bundle directory" ), QString() );
    if ( bundleDir.isEmpty() )
        return;
    // Target experiment: the store's real experiments, chosen from them —
    // the importer refuses a missing target with a typed warning, so the
    // dialog only pre-filters obvious mistakes.
    QStringList experimentIds;
    {
        const auto page = m_liveStore->listExperiments( 0, 1000 );
        if ( page )
            for ( const auto &experiment : page.value().second )
                experimentIds.append( experiment.experimentId() );
    }
    if ( experimentIds.isEmpty() )
    {
        m_designerLog->setPlainText(
          tr( "The open store has no experiments; an import needs a target "
              "experiment to attach the evidence to." ) );
        return;
    }
    bool accepted = false;
    const QString targetExperimentId = QInputDialog::getItem(
      this, tr( "Target experiment" ), tr( "Experiment the imported evidence attaches to" ),
      experimentIds, 0, true, &accepted );
    if ( !accepted || targetExperimentId.isEmpty() )
        return;

    // Dry-run precheck FIRST: every gate runs, nothing is written; the
    // conflicts/warnings the user must see are exactly what the engine
    // reports (D4 — no UI-side reimplementation of any gate).
    const auto precheck =
      importReproductionBundleAtPath( bundleDir, targetExperimentId, false, true );
    QString precheckText =
      tr( "Dry-run precheck for %1 → experiment %2:\n" ).arg( bundleDir, targetExperimentId );
    precheckText += tr( "would install run %1 (original %2)%3\n" )
                      .arg( precheck.runId,
                            precheck.originalRunId,
                            precheck.alreadyPresent
                              ? tr( " — already present, re-import is idempotent" )
                              : QString() );
    for ( const QString &conflict : precheck.conflicts )
        precheckText += tr( "conflict: %1\n" ).arg( conflict );
    for ( const QString &warning : precheck.warnings )
        precheckText += tr( "warning: %1\n" ).arg( warning );
    m_designerLog->setPlainText( precheckText );
    if ( !precheck.ok )
        return; // integrity/schema gates refused — nothing to confirm

    const QMessageBox::StandardButton choice = QMessageBox::question(
      this, tr( "Import reproduction bundle" ),
      tr( "%1\n\nImport now? The run lands as Created evidence (never as executed)." )
        .arg( precheckText ),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
    if ( choice != QMessageBox::Yes )
        return;

    const auto result =
      importReproductionBundleAtPath( bundleDir, targetExperimentId, false, false );
    QString resultText =
      tr( "Import %1: run %2 (original %3)%4\n" )
        .arg( result.ok ? tr( "ok" ) : tr( "refused" ), result.runId, result.originalRunId,
              result.alreadyPresent ? tr( " — already present, idempotent" ) : QString() );
    for ( const QString &conflict : result.conflicts )
        resultText += tr( "conflict: %1\n" ).arg( conflict );
    for ( const QString &warning : result.warnings )
        resultText += tr( "warning: %1\n" ).arg( warning );
    m_designerLog->setPlainText( resultText );
}

void ExperimentStudioDock::runLiveStudy()
{
    if ( !m_liveStore )
    {
        m_designerLog->setPlainText( tr( "Open an experiment store first" ) );
        return;
    }
    if ( liveBusy() )
    {
        m_designerLog->setPlainText( tr( "study run already in progress" ) );
        return;
    }
    const QString inputRaster = m_inputRasterEdit->text().trimmed();
    if ( inputRaster.isEmpty() || !QFile::exists( inputRaster ) )
    {
        m_designerLog->setPlainText(
            tr( "Input raster missing: the study needs a real input file" ) );
        return;
    }

    // The designer fields ARE the live spec — built by the SAME normalized
    // construction the design validation consumes (D1, R29-T04), then gated
    // by that same validation before anything is enqueued: an illegal
    // parameter never reaches TaskCenter, and the run's fingerprint is the
    // fingerprint the validated draft showed. Typed study.* refusals from
    // the production reader inside StudyRunner still surface verbatim.
    const ParameterStudySpec spec = designerSpecFromFields( inputRaster );
    const QStringList designIssues = validateDesignerSpec( spec );
    if ( !designIssues.isEmpty() )
    {
        logTypedFailure( m_designerLog, QStringLiteral( "run study" ),
                         designIssues.first().section( QLatin1Char( ':' ), 0, 0 ),
                         designIssues.join( QStringLiteral( "; " ) ) );
        return;
    }
    m_liveSpatialEpsilon = 0.0;

    // Sampling happens on the UI thread (pure, bounded); a typed refusal
    // never reaches the worker. Safe to hand the same points to
    // reportFromStore because sampling is deterministic (seeded); StudyRunner
    // re-samples internally and the exemplar tests pin rows/run-ids aligned.
    const auto points = sampleStudyPoints( spec );
    if ( !points )
    {
        logTypedFailure( m_designerLog, QStringLiteral( "sample study" ),
                         points.diagnostics().first().code,
                         points.diagnostics().first().message );
        return;
    }

    m_runLiveBtn->setEnabled( false );
    m_openStoreBtn->setEnabled( false );
    m_importBundleBtn->setEnabled( false );
    m_liveCancel = std::make_shared<std::atomic<bool>>( false );
    auto cancel = m_liveCancel;
    auto store = m_liveStore;
    auto ledger = m_liveLedger;
    const QString outputDir = m_liveStudyOutputDir;
    auto alive = m_aliveFlag;
    // Constructed on the GUI thread and only ever READ on the GUI thread
    // (inside the delivered lambda): the worker never dereferences the
    // dock, so a dock destroyed mid-run is simply a dropped delivery.
    QPointer<ExperimentStudioDock> self( this );

    m_liveRunThread = std::make_unique<std::thread>(
        [self, alive, store, ledger, spec, pts = points.value(), outputDir, cancel]() {
            QJsonObject reportJson;
            QString error;
            int recorded = 0;
            int failed = 0;
            int cancelledCount = 0;
            try
            {
                // Waiter thread only: StudyRunner owns the in-flight window and
                // TaskCenter owns admission. No study point executes here.
                ExecutionPlaneStudyBackend backend; // production spine adapter
                const auto ran =
                    runStudy( *store, *ledger, spec, backend, outputDir, *cancel );
                if ( ran )
                {
                    recorded = ran->recordedCount;
                    failed = ran->failedCount;
                    cancelledCount = ran->cancelledCount;
                    if ( spec.spatialComparison && ran->recordedCount > 0 )
                    {
                        // Real GDAL run-vs-baseline summaries for the report.
                        const GdalRasterDifferenceSummarizer summarizer( spec.spatialEpsilon );
                        const auto spatial =
                            summarizeStudyOutputs( *store, *ledger, spec, pts, outputDir,
                                                   summarizer );
                        const StudyReport report =
                            reportFromStore( *store, *ledger, spec, pts,
                                             spatial ? spatial.value()
                                                     : QVector<SpatialDifferenceSummary>{},
                                             &ran.value() );
                        reportJson = report.toJson();
                    }
                    else
                    {
                        const StudyReport report =
                            reportFromStore( *store, *ledger, spec, pts, {}, &ran.value() );
                        reportJson = report.toJson();
                    }
                }
                else
                {
                    error = QStringLiteral( "%1: %2" )
                                .arg( ran.diagnostics().first().code,
                                      ran.diagnostics().first().message );
                }
            }
            catch ( const std::exception &e )
            {
                error = QString::fromUtf8( e.what() );
            }
            catch ( ... )
            {
                error = QStringLiteral( "Unknown fatal exception in live study execution" );
            }

            if ( !alive->load( std::memory_order_acquire ) )
                return; // dock closed mid-run; refs keep the stores alive

            // qApp context + GUI-thread QPointer read (#1805 F18): the
            // delivery survives a dock destroyed after this point without
            // touching freed memory.
            QMetaObject::invokeMethod(
                qApp,
                [self, alive, reportJson, recorded, failed, cancelledCount, error]() {
                    if ( self && alive->load( std::memory_order_acquire ) )
                        self->applyLiveStudyResult( reportJson, recorded, failed,
                                                    cancelledCount, error );
                } );
        } );
}

void ExperimentStudioDock::pruneRetiredStudyThreadsLocked()
{
    for ( auto it = m_retiredStudyThreads.begin(); it != m_retiredStudyThreads.end(); )
    {
        if ( it->done && it->done->load( std::memory_order_acquire ) )
        {
            if ( it->thread.joinable() )
                it->thread.join();
            it = m_retiredStudyThreads.erase( it );
        }
        else
        {
            ++it;
        }
    }
}

void ExperimentStudioDock::applyLiveStudyResult( const QJsonObject &reportJson, int recordedCount,
                                                 int failedCount, int cancelledCount,
                                                 const QString &error )
{
    if ( m_liveRunThread && m_liveRunThread->joinable() )
    {
        std::lock_guard<std::mutex> lock( m_retiredStudyMutex );
        pruneRetiredStudyThreadsLocked();
        auto done = std::make_shared<std::atomic<bool>>( false );
        std::thread reaper( [t = std::move( m_liveRunThread ), done]() mutable {
            if ( t && t->joinable() )
                t->join();
            done->store( true, std::memory_order_release );
        } );
        m_retiredStudyThreads.push_back( { std::move( reaper ), done } );
    }
    m_liveRunThread.reset();
    m_liveCancel.reset();
    m_runLiveBtn->setEnabled( true );
    m_openStoreBtn->setEnabled( true );
    m_importBundleBtn->setEnabled( m_liveStore != nullptr );

    if ( !error.isEmpty() )
    {
        logTypedFailure( m_designerLog, QStringLiteral( "live study" ),
                         QStringLiteral( "study.run_failed" ), error );
        return;
    }

    m_lastStudyReport = reportJson;
    m_session.lastStudyReport = reportJson;
    const auto report = StudyReport::fromJson( reportJson );
    if ( report )
    {
        m_session.studyId = report->studyId;
        m_session.experimentId = report->experimentId;
        m_session.algorithmId = report->algorithmId;
    }
    savePersistedSession();
    rebuildMatrixTable();
    refreshSensitivity();
    m_tabs->setCurrentIndex( 1 );
    m_matrixStatus->setText(
        tr( "live study: %1 recorded, %2 failed, %3 cancelled — run truth in the store" )
            .arg( recordedCount )
            .arg( failedCount )
            .arg( cancelledCount ) );
}

void ExperimentStudioDock::logTypedFailure( QPlainTextEdit *log, const QString &context,
                                            const QString &code, const QString &message )
{
    if ( !log )
        return;
    log->setPlainText(
        QStringLiteral( "%1 refused — %2: %3" ).arg( context, code, message ) );
}

bool ExperimentStudioDock::liveBusy() const
{
    return m_liveRunThread != nullptr;
}

void ExperimentStudioDock::cancelLiveStudy()
{
    if ( !liveBusy() || !m_liveCancel )
    {
        m_matrixStatus->setText( tr( "No live study in progress" ) );
        return;
    }
    // Cooperative cancel: StudyRunner observes the flag between submissions,
    // drains in-flight points as cancelled and records truthful states.
    m_liveCancel->store( true );
    m_matrixStatus->setText(
        tr( "Cancellation requested — propagates via StudyRunner → TaskCenter" ) );
}

void ExperimentStudioDock::compareSelectedSpatial()
{
    const auto selected = m_matrixTable->selectionModel()->selectedRows();
    // LIVE path: a live study exists and two recorded points are selected →
    // compare the COMMITTED outputs through the real GDAL summarizer.
    if ( m_liveStore && !m_lastStudyReport.isEmpty() && selected.size() >= 2 )
    {
        const auto *item0 = m_matrixTable->item( selected[0].row(), 0 );
        const auto *item1 = m_matrixTable->item( selected[1].row(), 0 );
        if ( !item0 || !item1 )
            return;
        const QString leftId = item0->text();
        const QString rightId = item1->text();
        const auto vm = compareRecordedPoints( m_liveStudyOutputDir, leftId, rightId,
                                               m_liveSpatialEpsilon, SpatialCompareMode::DiffSummary,
                                               m_session.policy );
        if ( !vm )
        {
            logTypedFailure( m_spatialLog, QStringLiteral( "compare" ),
                             vm.diagnostics().first().code,
                             vm.diagnostics().first().message );
            return;
        }
        m_lastSpatialVm = vm.value().toJson();
        m_spatialLog->setPlainText( QString::fromUtf8(
            QJsonDocument( m_lastSpatialVm ).toJson( QJsonDocument::Indented ) ) );
        return;
    }

    // DEMO path (no live store): labeled demo, refuses real paths.
    QString left = QStringLiteral( "synthetic://out/p0000.tif" );
    QString right = QStringLiteral( "synthetic://out/p0001.tif" );
    QString leftId = QStringLiteral( "p0000" );
    QString rightId = QStringLiteral( "p0001" );
    if ( selected.size() >= 2 )
    {
        const auto *item0 = m_matrixTable->item( selected[0].row(), 0 );
        const auto *item1 = m_matrixTable->item( selected[1].row(), 0 );
        if ( item0 && item1 )
        {
            leftId = item0->text();
            rightId = item1->text();
            left = QStringLiteral( "synthetic://out/%1.tif" ).arg( leftId );
            right = QStringLiteral( "synthetic://out/%1.tif" ).arg( rightId );
        }
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
    {
        // Honest empty state instead of silently substituting the synthetic
        // demo: a refresh must never paint demo curves as if they were the
        // user's study (the constructor still opens with the visible demo).
        m_sensitivityLog->setPlainText(
          tr( "No report loaded — open a live study or press 'Load NDVI/threshold "
              "demo' to inspect the synthetic sample." ) );
        m_lastSensitivityVm = QJsonObject();
        rebuildChart();
        return;
    }
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
        for ( const auto &v : points )
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
    // LIVE fault teaching: a real sicnu.lab.faults/1 scenario runs through
    // the REAL faultlab sandbox pipeline (copy → inject into the copy →
    // re-digest the source). No scenario file → typed refusal, never a
    // fabricated run.
    const QString scenarioPath = QFileDialog::getOpenFileName(
        this, tr( "Load fault scenario (sicnu.lab.faults/1)" ), QString(),
        tr( "Scenario JSON (*.json)" ) );
    if ( scenarioPath.isEmpty() )
    {
        m_faultLog->setPlainText(
            tr( "No scenario loaded — pick a sicnu.lab.faults/1 JSON document "
                "(the previous all-nodata demo ran no real sandbox)." ) );
        return;
    }
    QFile scenarioFile( scenarioPath );
    if ( !scenarioFile.open( QIODevice::ReadOnly ) )
    {
        logTypedFailure( m_faultLog, QStringLiteral( "load scenario" ),
                         QStringLiteral( "experiment_studio.fault_scenario_unreadable" ),
                         scenarioPath );
        return;
    }
    QJsonParseError parseError;
    const QJsonObject scenario =
        QJsonDocument::fromJson( scenarioFile.readAll(), &parseError ).object();
    if ( scenario.isEmpty() )
    {
        logTypedFailure( m_faultLog, QStringLiteral( "parse scenario" ),
                         QStringLiteral( "experiment_studio.fault_scenario_invalid" ),
                         parseError.errorString() );
        return;
    }
    const QString prediction = m_faultPrediction->text().trimmed();
    const auto vm = runFaultScenarioTeaching( scenario, prediction );
    if ( !vm )
    {
        logTypedFailure( m_faultLog, QStringLiteral( "run scenario" ),
                         vm.diagnostics().first().code, vm.diagnostics().first().message );
        return;
    }
    m_lastFaultVm = vm.value().toJson();
    m_session.faultScenarioId = vm->scenarioId;
    m_faultLog->setPlainText(
        QString::fromUtf8( QJsonDocument( m_lastFaultVm ).toJson( QJsonDocument::Indented ) ) );
}

void ExperimentStudioDock::loadFirstDivergenceDemo()
{
    // LIVE divergence: real recorded evidence via DirectoryEvidenceSource —
    // checkpoint/provenance/workflow-metrics when present, typed evidence
    // gaps (and DOWNGRADED confidence) when absent. Without a live study,
    // this stays a clearly-labeled demo, never fake evidence.
    if ( !m_liveStore || m_lastStudyReport.isEmpty() )
    {
        m_divergenceLog->setPlainText(
            tr( "No live study — open a store, run a study, then compare two "
                "recorded runs. (The previous hand-built report was a demo.)" ) );
        return;
    }
    if ( liveBusy() )
    {
        m_divergenceLog->setPlainText( tr( "study run in progress — wait or cancel first" ) );
        return;
    }
    QString referenceRunId = m_session.referenceRunId;
    QString studentRunId = m_session.studentRunId;
    const auto selected = m_matrixTable->selectionModel()->selectedRows();
    if ( selected.size() >= 2 )
    {
        const auto report = StudyReport::fromJson( m_lastStudyReport );
        if ( report )
        {
            const auto *item0 = m_matrixTable->item( selected[0].row(), 0 );
            const auto *item1 = m_matrixTable->item( selected[1].row(), 0 );
            if ( item0 && item1 )
            {
                const QString leftPointId = item0->text();
                const QString rightPointId = item1->text();
                for ( const StudyRunRow &row : report->runTable )
                {
                    if ( row.pointId == leftPointId && !row.runId.isEmpty() )
                        referenceRunId = row.runId;
                    if ( row.pointId == rightPointId && !row.runId.isEmpty() )
                        studentRunId = row.runId;
                }
            }
        }
    }
    if ( referenceRunId.isEmpty() || studentRunId.isEmpty() || referenceRunId == studentRunId )
    {
        m_divergenceLog->setPlainText(
            tr( "Select two recorded points in the run matrix (reference and "
                "student run) to localize divergence." ) );
        return;
    }
    const auto report =
        firstDivergenceReport( m_liveStore.get(), m_liveStudyOutputDir, referenceRunId,
                               studentRunId );
    if ( !report )
    {
        logTypedFailure( m_divergenceLog, QStringLiteral( "first divergence" ),
                         report.diagnostics().first().code,
                         report.diagnostics().first().message );
        return;
    }
    const FirstDivergenceViewModel vm = projectFirstDivergence( report.value() );
    // No synthetic marker here: this IS analyzer output over recorded runs.
    // (The #1293 merge stranded the old demo marker on this live path, which
    // exported real evidence labeled "not derived from recorded runs".)
    m_lastDivergenceVm = vm.toJson();
    // Provenance honesty: this VM IS analyzer output over recorded runs — the
    // synthetic marker belongs ONLY on documents derived from no recorded run
    // (the demo study report). Stamping it here mislabeled live evidence as a
    // hand-built demo and the marker leaked into export bundles that way.
    m_session.referenceRunId = vm.referenceRunId;
    m_session.studentRunId = vm.studentRunId;
    m_divergenceLog->setPlainText(
        QString::fromUtf8( QJsonDocument( m_lastDivergenceVm ).toJson( QJsonDocument::Indented ) ) );
}

void ExperimentStudioDock::exportBundle()
{
    if ( m_lastStudyReport.isEmpty() )
    {
        // Honesty over convenience: exporting silently swapped in synthetic
        // demo data whenever no study was loaded — a bundle the user never
        // asked for. Refuse VISIBLY (studyError only builds the diagnostic;
        // the log is what the user reads).
        const auto refusal = studyError(
          QStringLiteral( "experiment_studio.export_no_study" ),
          QStringLiteral( "no study report loaded — load a live study (or press the demo "
                          "button) before exporting" ) );
        m_exportLog->setPlainText( refusal.code + QStringLiteral( ": " ) + refusal.message );
        return;
    }
    StudioExportBundle bundle;
    bundle.studyId = m_session.studyId;
    bundle.studyReport = m_lastStudyReport;
    bundle.faultTeaching = m_lastFaultVm;
    bundle.firstDivergence = m_lastDivergenceVm;
    bundle.designer = m_lastDesignerVm;
    bundle.runMatrix = m_lastMatrixVm;
    const QJsonArray table = m_lastStudyReport.value( QStringLiteral( "run_table" ) ).toArray();
    for ( const auto &v : table )
    {
        const QString runId = v.toObject().value( QStringLiteral( "run_id" ) ).toString();
        if ( !runId.isEmpty() )
            bundle.runIds.append( runId );
    }
    bundle.csvRunTable = studyRunTableToCsv( m_lastStudyReport );

    // LIVE capsules: build/export/reload through the real Capsule APIs for
    // the recorded divergence runs. The bundle and session carry the REFS
    // (paths) only — a capsule body never enters either.
    QStringList capsuleLogLines;
    if ( m_liveStore && !liveBusy() )
    {
        QStringList capsuleRunIds;
        if ( !m_session.referenceRunId.isEmpty() )
            capsuleRunIds.append( m_session.referenceRunId );
        if ( !m_session.studentRunId.isEmpty()
             && !capsuleRunIds.contains( m_session.studentRunId ) )
            capsuleRunIds.append( m_session.studentRunId );
        const QString capsuleDir = m_liveStudyOutputDir + QStringLiteral( "/capsules" );
        for ( const QString &runId : capsuleRunIds )
        {
            const QString capsulePath =
                capsuleDir + QStringLiteral( "/capsule-%1.json" ).arg( runId );
            const auto exported = exportRunCapsule( *m_liveStore, *m_liveDatasets, runId,
                                                    capsulePath, m_liveStudyOutputDir );
            if ( !exported )
            {
                capsuleLogLines.append(
                    QStringLiteral( "capsule %1 refused — %2: %3" )
                        .arg( runId, exported.diagnostics().first().code,
                              exported.diagnostics().first().message ) );
                continue;
            }
            bundle.capsuleRefs.append( exported->path );
            const auto readiness = capsuleReloadReadiness( exported->path, nullptr );
            if ( readiness )
            {
                capsuleLogLines.append(
                    QStringLiteral( "capsule %1 → %2 (readiness level: %3)" )
                        .arg( runId, exported->path,
                              readiness->value( QStringLiteral( "readiness" ) )
                                  .toObject()
                                  .value( QStringLiteral( "level" ) )
                                  .toString() ) );
            }
            else
            {
                capsuleLogLines.append(
                    QStringLiteral( "capsule reload %1 refused — %2: %3" )
                        .arg( runId, readiness.diagnostics().first().code,
                              readiness.diagnostics().first().message ) );
            }
        }
    }

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
    savePersistedSession();
    m_exportLog->setPlainText(
        tr( "Wrote %1\nCSV rows embedded; offline reload via StudioExportBundle::fromJson\n"
            "capsule refs: %2" )
            .arg( path )
            .arg( bundle.capsuleRefs.isEmpty()
                      ? QStringLiteral( "(none — no live store)" )
                      : bundle.capsuleRefs.join( QStringLiteral( ", " ) ) ) );
    for ( const QString &line : capsuleLogLines )
        m_exportLog->appendPlainText( line );
}

} // namespace sicnu::app
