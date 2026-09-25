#include "teaching_admin_dock.h"

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/class_summary.h"
#include "teaching_admin/curriculum_editor.h"
#include "teaching_admin/data_pack_manager.h"
#include "teaching_admin/feedback_pack.h"
#include "teaching_admin/grader_cli_adapter.h"
#include "teaching_admin/labspec_authoring.h"
#include "teaching_admin/operator_catalog.h"
#include "teaching_admin/release_preflight.h"
#include "teaching_admin/rubric_builder.h"
#include "teaching_admin/script_adapters.h"
#include "teaching_admin/student_projection.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QVBoxLayout>
#include <atomic>

namespace sicnu::app::teaching_admin {

TeachingAdminDock::~TeachingAdminDock()
{
    // A destroyed console must not leave a batch thread touching `this`.
    m_batchCancel.store( true );
    if ( m_batchThread.joinable() )
        m_batchThread.join();
}

TeachingAdminDock::TeachingAdminDock( QWidget *parent )
    : QWidget( parent )
{
    auto *root = new QVBoxLayout( this );
    m_tabs = new QTabWidget( this );
    root->addWidget( m_tabs, 1 );

    // --- A Course Builder ---
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout( page );
        m_curriculumEdit = new QPlainTextEdit;
        m_curriculumEdit->setPlaceholderText( tr( "Paste sicnu.curriculum/1 JSON…" ) );
        lay->addWidget( m_curriculumEdit, 1 );
        auto *row = new QHBoxLayout;
        m_labsDirEdit = new QLineEdit( QDir( repoRoot() ).filePath( QStringLiteral( "data/labs" ) ) );
        row->addWidget( new QLabel( tr( "labs dir" ) ) );
        row->addWidget( m_labsDirEdit, 1 );
        auto *btn = new QPushButton( tr( "Validate + Preview" ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onValidateCurriculum );
        row->addWidget( btn );
        lay->addLayout( row );
        m_tabs->addTab( page, tr( "A 课程" ) );
    }

    // --- B Lab Authoring ---
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout( page );
        m_labSpecEdit = new QPlainTextEdit;
        m_labSpecEdit->setPlaceholderText( tr( "Paste LabSpec JSON…" ) );
        lay->addWidget( m_labSpecEdit, 1 );
        // Real registry truth only: operator ids/param schemas come from the
        // repo's capability sidecars — never a hand-typed allow-list.
        m_operatorRegistryLabel = new QLabel;
        m_operatorRegistryLabel->setObjectName( QStringLiteral( "teachingAdminOperatorRegistry" ) );
        m_operatorRegistryLabel->setWordWrap( true );
        lay->addWidget( m_operatorRegistryLabel );
        auto *row = new QHBoxLayout;
        auto *btn = new QPushButton( tr( "Validate LabSpec" ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onValidateLabSpec );
        row->addWidget( btn );
        lay->addLayout( row );
        m_tabs->addTab( page, tr( "B 实验" ) );
    }

    // --- C Rubric ---
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout( page );
        m_rubricKindHint = new QLabel(
            tr( "Supports lab rules (sicnu.lab.rules/1) or process rubric (sicnu.grader.rubric/1)." ) );
        lay->addWidget( m_rubricKindHint );
        m_rubricEdit = new QPlainTextEdit;
        lay->addWidget( m_rubricEdit, 1 );
        auto *btn = new QPushButton( tr( "Validate Rubric" ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onValidateRubric );
        lay->addWidget( btn );
        m_tabs->addTab( page, tr( "C 量规" ) );
    }

    // --- D Packs ---
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout( page );
        m_packsDirEdit = new QLineEdit( QDir( repoRoot() ).filePath( QStringLiteral( "data/labs/packs" ) ) );
        auto *row = new QHBoxLayout;
        row->addWidget( new QLabel( tr( "packs dir" ) ) );
        row->addWidget( m_packsDirEdit, 1 );
        auto *btn = new QPushButton( tr( "Inventory" ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onInventoryPacks );
        row->addWidget( btn );
        lay->addLayout( row );
        auto *driftBtn = new QPushButton( tr( "Check Foundry Drift (gen_lab_packs.py --check)" ) );
        driftBtn->setObjectName( QStringLiteral( "teachingAdminPackDriftButton" ) );
        driftBtn->setToolTip( tr( "Reuses the pack foundry contract: exit 0 = in sync, DRIFT lines are listed." ) );
        connect( driftBtn, &QPushButton::clicked, this, &TeachingAdminDock::onCheckPackDrift );
        lay->addWidget( driftBtn );
        lay->addStretch( 1 );
        m_tabs->addTab( page, tr( "D 数据包" ) );
    }

    // --- E Preflight ---
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout( page );
        m_softwareVersionEdit = new QLineEdit( QStringLiteral( "exp-rs-dev" ) );
        lay->addWidget( new QLabel( tr( "software version" ) ) );
        lay->addWidget( m_softwareVersionEdit );
        auto *btn = new QPushButton( tr( "Run Preflight (uses A/B/C/D editors)" ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onRunPreflight );
        lay->addWidget( btn );
        lay->addStretch( 1 );
        m_tabs->addTab( page, tr( "E 预检" ) );
    }

    // --- F Offline ---
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout( page );
        m_bundleDirEdit = new QLineEdit;
        m_bundleDirEdit->setPlaceholderText( tr( "Path to assembled offline bundle dir" ) );
        lay->addWidget( m_bundleDirEdit );
        auto *btn = new QPushButton( tr( "Verify Bundle Manifest" ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onVerifyBundle );
        lay->addWidget( btn );
        lay->addWidget( new QLabel(
            tr( "Build uses scripts/build_offline_bundle.sh via structured adapter (not reimplemented)." ) ) );
        lay->addStretch( 1 );
        m_tabs->addTab( page, tr( "F 离线包" ) );
    }

    // --- G Batch ---
    {
        auto *page = new QWidget;
        auto *lay = new QFormLayout( page );
        m_submissionsDirEdit = new QLineEdit;
        m_submissionsDirEdit->setObjectName( QStringLiteral( "teachingAdminSubmissionsDir" ) );
        m_labIdEdit = new QLineEdit( QStringLiteral( "lab15_data_inspection" ) );
        m_labIdEdit->setObjectName( QStringLiteral( "teachingAdminLabId" ) );
        m_batchOutEdit = new QLineEdit( QDir::temp().filePath( QStringLiteral( "teaching_admin_batch" ) ) );
        m_batchOutEdit->setObjectName( QStringLiteral( "teachingAdminBatchOut" ) );
        lay->addRow( tr( "submissions dir" ), m_submissionsDirEdit );
        lay->addRow( tr( "lab id" ), m_labIdEdit );
        lay->addRow( tr( "out prefix" ), m_batchOutEdit );
        auto *btn = new QPushButton( tr( "Batch Grade (real grader CLI)" ) );
        btn->setObjectName( QStringLiteral( "teachingAdminBatchButton" ) );
        btn->setToolTip(
            tr( "Grades via sicnu_geo_rs_cli lab --grade (OutputVerifier authority). "
                "Missing CLI ⇒ typed unavailable rows, never fabricated scores." ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onRunBatch );
        lay->addRow( btn );
        m_batchCancelButton = new QPushButton( tr( "Cancel Batch" ) );
        m_batchCancelButton->setObjectName( QStringLiteral( "teachingAdminBatchCancelButton" ) );
        m_batchCancelButton->setEnabled( false );
        m_batchCancelButton->setToolTip(
            tr( "Stops before the next not-yet-started submission: completed rows are kept, "
                "never-started rows are typed cancelled (never disguised failures)." ) );
        connect( m_batchCancelButton, &QPushButton::clicked, this, &TeachingAdminDock::onCancelBatch );
        lay->addRow( m_batchCancelButton );
        m_batchProgressLabel = new QLabel( tr( "idle" ) );
        m_batchProgressLabel->setObjectName( QStringLiteral( "teachingAdminBatchProgress" ) );
        lay->addRow( m_batchProgressLabel );
        // Progress is push-based (worker → queued UI update); the timer
        // member stays for future pull-based diagnostics.
        m_tabs->addTab( page, tr( "G 批量评分" ) );
    }

    // --- H/I Feedback + Summary ---
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Build Feedback Packs + Class Summary from last batch out" ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onBuildFeedbackAndSummary );
        lay->addWidget( btn );
        lay->addStretch( 1 );
        m_tabs->addTab( page, tr( "H/I 反馈与统计" ) );
    }

    m_log = new QPlainTextEdit;
    m_log->setObjectName( QStringLiteral( "teachingAdminLog" ) );
    m_log->setReadOnly( true );
    m_log->setMaximumBlockCount( 2000 );
    root->addWidget( new QLabel( tr( "Console" ) ) );
    root->addWidget( m_log, 1 );

    refreshOperatorRegistryLabel();
}

QString TeachingAdminDock::repoRoot() const
{
    const QByteArray env = qgetenv( "SICNU_SOURCE_DIR" );
    if ( !env.isEmpty() )
        return QString::fromLocal8Bit( env );
    QDir d( QCoreApplication::applicationDirPath() );
    for ( int i = 0; i < 6; ++i )
    {
        if ( QFileInfo::exists( d.filePath( QStringLiteral( "data/labs/lab-registry.json" ) ) ) )
            return d.absolutePath();
        if ( !d.cdUp() )
            break;
    }
    return QDir::currentPath();
}

void TeachingAdminDock::appendLog( const QString &text )
{
    m_log->appendPlainText( text );
}

sicnu::teaching_admin::OperatorCatalog TeachingAdminDock::loadOperatorCatalog() const
{
    return sicnu::teaching_admin::loadOperatorCatalog(
        QDir( repoRoot() ).filePath( QStringLiteral( "data/processing/algorithm_meta/capability" ) ) );
}

void TeachingAdminDock::refreshOperatorRegistryLabel()
{
    if ( !m_operatorRegistryLabel )
        return;
    const auto catalog = loadOperatorCatalog();
    m_operatorRegistryLabel->setText(
        catalog.operatorIds.isEmpty()
          ? tr( "operator registry: unavailable (%1 issues) — validation stays fail-closed" )
              .arg( catalog.issues.size() )
          : tr( "operator registry: %1 operators (capability sidecars)" )
              .arg( catalog.operatorIds.size() ) );
}

void TeachingAdminDock::onValidateCurriculum()
{
    const auto doc = QJsonDocument::fromJson( m_curriculumEdit->toPlainText().toUtf8() );
    if ( !doc.isObject() )
    {
        appendLog( tr( "curriculum: invalid JSON" ) );
        return;
    }
    using namespace sicnu::teaching_admin;
    CurriculumPaths paths;
    paths.labsDir = m_labsDirEdit->text();
    paths.packsDir = QDir( paths.labsDir ).filePath( QStringLiteral( "packs" ) );
    paths.registryPath = QDir( paths.labsDir ).filePath( QStringLiteral( "lab-registry.json" ) );
    const auto labs = discoverKnownLabIds( paths );
    const auto packs = discoverKnownPackIds( paths );
    const auto vr = validateCurriculum( doc.object(), labs, packs );
    appendLog( QString::fromUtf8( QJsonDocument( vr.toJson() ).toJson( QJsonDocument::Compact ) ) );
    const auto preview = projectCourseHomePreview( doc.object() );
    appendLog( tr( "preview modules: %1" ).arg( preview.value( QStringLiteral( "modules" ) ).toArray().size() ) );
}

void TeachingAdminDock::onValidateLabSpec()
{
    const auto doc = QJsonDocument::fromJson( m_labSpecEdit->toPlainText().toUtf8() );
    if ( !doc.isObject() )
    {
        appendLog( tr( "labspec: invalid JSON" ) );
        return;
    }
    const auto catalog = loadOperatorCatalog();
    refreshOperatorRegistryLabel();
    const auto vr = sicnu::teaching_admin::validateLabSpec( doc.object(), catalog.operatorIds,
                                                            catalog.paramSchemas, repoRoot() );
    appendLog( QString::fromUtf8( QJsonDocument( vr.toJson() ).toJson( QJsonDocument::Compact ) ) );
    // Both projections derive from the same authoring truth: the teacher sees
    // the recipe compile view, the student-side view is answer-masked.
    const auto recipe = sicnu::teaching_admin::projectRecipeCompileView( doc.object() );
    appendLog( QString::fromUtf8( QJsonDocument( recipe ).toJson( QJsonDocument::Compact ) ) );
    const auto student = sicnu::teaching_admin::projectStudentLabView( doc.object() );
    appendLog( QString::fromUtf8( QJsonDocument( student ).toJson( QJsonDocument::Compact ) ) );
}

void TeachingAdminDock::onValidateRubric()
{
    const auto doc = QJsonDocument::fromJson( m_rubricEdit->toPlainText().toUtf8() );
    if ( !doc.isObject() )
    {
        appendLog( tr( "rubric: invalid JSON" ) );
        return;
    }
    const QJsonObject obj = doc.object();
    sicnu::teaching_admin::ValidationResult vr;
    if ( obj.value( QStringLiteral( "schema" ) ).toString() == QLatin1String( "sicnu.grader.rubric/1" ) )
        vr = sicnu::teaching_admin::validateProcessRubric( obj );
    else
        vr = sicnu::teaching_admin::validateLabRules( obj );
    appendLog( QString::fromUtf8( QJsonDocument( vr.toJson() ).toJson( QJsonDocument::Compact ) ) );
}

void TeachingAdminDock::onInventoryPacks()
{
    const auto inv = sicnu::teaching_admin::inventoryPacks( m_packsDirEdit->text(), repoRoot() );
    appendLog( QString::fromUtf8( QJsonDocument( inv.toJson() ).toJson( QJsonDocument::Compact ) ) );
}

void TeachingAdminDock::onCheckPackDrift()
{
    const auto check = sicnu::teaching_admin::checkLabPackDrift( repoRoot() );
    appendLog( tr( "foundry drift: %1" ).arg( check.summary ) );
    appendLog( QString::fromUtf8( QJsonDocument( check.toJson() ).toJson( QJsonDocument::Compact ) ) );
}

void TeachingAdminDock::onRunPreflight()
{
    using namespace sicnu::teaching_admin;
    PreflightInput in;
    in.softwareVersion = m_softwareVersionEdit->text();
    in.repoRoot = repoRoot();
    const auto cur = QJsonDocument::fromJson( m_curriculumEdit->toPlainText().toUtf8() );
    if ( cur.isObject() )
        in.curriculum = cur.object();
    const auto lab = QJsonDocument::fromJson( m_labSpecEdit->toPlainText().toUtf8() );
    if ( lab.isObject() )
        in.labSpec = lab.object();
    const auto rub = QJsonDocument::fromJson( m_rubricEdit->toPlainText().toUtf8() );
    if ( rub.isObject() )
    {
        if ( rub.object().value( QStringLiteral( "schema" ) ).toString() == QLatin1String( "sicnu.grader.rubric/1" ) )
            in.processRubric = rub.object();
        else
            in.labRules = rub.object();
    }
    const auto catalog = loadOperatorCatalog();
    in.knownOperators = catalog.operatorIds;
    in.operatorParamSchemas = catalog.paramSchemas;
    CurriculumPaths paths;
    paths.labsDir = m_labsDirEdit->text();
    paths.packsDir = QDir( paths.labsDir ).filePath( QStringLiteral( "packs" ) );
    paths.registryPath = QDir( paths.labsDir ).filePath( QStringLiteral( "lab-registry.json" ) );
    in.knownLabIds = discoverKnownLabIds( paths );
    const auto report = runPreflight( in );
    appendLog( QString::fromUtf8( QJsonDocument( report.toJson() ).toJson( QJsonDocument::Indented ) ) );
    appendLog( tr( "release digest: %1" ).arg( report.canonicalDigest() ) );
}

void TeachingAdminDock::onVerifyBundle()
{
    const auto r = sicnu::teaching_admin::verifyOfflineBundle( repoRoot(), m_bundleDirEdit->text() );
    appendLog( QString::fromUtf8( QJsonDocument( r.toJson() ).toJson( QJsonDocument::Compact ) ) );
}

void TeachingAdminDock::onRunBatch()
{
    using namespace sicnu::teaching_admin;
    if ( m_batchThread.joinable() )
    {
        appendLog( tr( "a batch is already running — cancel it first" ) );
        return;
    }

    BatchAssessmentConfig cfg;
    cfg.labId = m_labIdEdit->text();
    cfg.submissionsDir = m_submissionsDirEdit->text();
    cfg.outDir = QFileInfo( m_batchOutEdit->text() ).path();
    cfg.rubricVersion = QStringLiteral( "ui-1" );
    cfg.labVersion = QStringLiteral( "ui-1" );
    cfg.softwareVersion = m_softwareVersionEdit->text();
    // Durable partial results beside the published outputs: a re-run over
    // the same class resumes instead of re-grading completed submissions,
    // and a cancelled run still publishes the completed rows.
    cfg.checkpointPath = m_batchOutEdit->text() + QStringLiteral( ".checkpoint.json" );
    m_batchCancel.store( false );

    // Real grading authority only: the lab CLI shell of
    // OutputVerifier::gradeArtifact. When the CLI is absent every row is
    // typed unavailable with a reason — the console never scores by itself.
    GraderCliConfig grader;
    grader.cliPath = resolveGraderCli();
    grader.labIdOrRulesPath = m_labIdEdit->text();
    if ( grader.cliPath.isEmpty() )
        appendLog( tr( "grader CLI not found (set SICNU_GEO_RS_CLI); rows will be typed unavailable" ) );

    const int total = sicnu::teaching_admin::discoverSubmissions( cfg.submissionsDir ).size();
    m_batchProgressLabel->setText( tr( "running 0 / %1" ).arg( total ) );
    m_batchCancelButton->setEnabled( true );

    const QString outPrefix = m_batchOutEdit->text();
    m_batchThread = std::thread( [this, cfg, grader, outPrefix]() {
        // Exception barrier: a throw leaving a std::thread function is
        // std::terminate — the GUI must survive a failed batch.
        try
        {
            sicnu::teaching_admin::BatchProgressFn progress =
              [this]( int done, int processedTotal ) {
                  QMetaObject::invokeMethod(
                    this,
                    [this, done, processedTotal]() {
                        m_batchProgressLabel->setText(
                          tr( "running %1 / %2" ).arg( done ).arg( processedTotal ) );
                    },
                    Qt::QueuedConnection );
              };
            const auto report =
              runBatchAssessment( cfg, cliGradeCallable( grader ), &m_batchCancel, progress );
            publishBatchOutputsAtomic( report, outPrefix );
            QMetaObject::invokeMethod(
              this,
              [this, report]() { finishBatchOnUiThread( report.toJson() ); },
              Qt::QueuedConnection );
        }
        catch ( const std::exception &e )
        {
            const QString what = QString::fromUtf8( e.what() );
            QMetaObject::invokeMethod(
              this, [this, what]() {
                  m_batchProgressLabel->setText( tr( "batch failed: %1" ).arg( what ) );
                  m_batchCancelButton->setEnabled( false );
                  appendLog( tr( "batch failed: %1" ).arg( what ) );
              },
              Qt::QueuedConnection );
        }
        catch ( ... )
        {
            QMetaObject::invokeMethod(
              this, [this]() {
                  m_batchProgressLabel->setText( tr( "batch failed (unknown error)" ) );
                  m_batchCancelButton->setEnabled( false );
                  appendLog( tr( "batch failed (unknown error)" ) );
              },
              Qt::QueuedConnection );
        }
    } );
}

void TeachingAdminDock::onCancelBatch()
{
    // Typed cooperation, not a kill: the orchestrator finishes the item in
    // flight, keeps completed rows, and types the remainder cancelled.
    m_batchCancel.store( true );
    m_batchCancelButton->setEnabled( false );
    m_batchProgressLabel->setText( m_batchProgressLabel->text() + tr( " — cancelling…" ) );
}



void TeachingAdminDock::finishBatchOnUiThread( QJsonObject reportJson )
{
    if ( m_batchThread.joinable() )
        m_batchThread.join();
    m_batchCancelButton->setEnabled( false );
    const QString state =
      reportJson.value( QStringLiteral( "cancelled_early" ) ).toBool()
        ? tr( "cancelled (durable partial results published)" )
        : tr( "finished" );
    m_batchProgressLabel->setText(
      tr( "%1 — graded %2 / %3" )
        .arg( state )
        .arg( reportJson.value( QStringLiteral( "graded" ) ).toInt() )
        .arg( reportJson.value( QStringLiteral( "total" ) ).toInt() ) );
    appendLog( QString::fromUtf8( QJsonDocument( reportJson ).toJson( QJsonDocument::Compact ) ) );

    // Class summary from the SAME published truth (the report on disk), not
    // from a parallel in-memory copy.
    QFile f( m_batchOutEdit->text() + QStringLiteral( ".json" ) );
    if ( f.open( QIODevice::ReadOnly ) )
    {
        sicnu::teaching_admin::BatchAssessmentReport report;
        const auto doc = QJsonDocument::fromJson( f.readAll() ).object();
        report.labId = doc.value( QStringLiteral( "lab_id" ) ).toString();
        report.total = doc.value( QStringLiteral( "total" ) ).toInt();
        report.rubricVersion = doc.value( QStringLiteral( "rubric_version" ) ).toString();
        report.labVersion = doc.value( QStringLiteral( "lab_version" ) ).toString();
        report.softwareVersion = doc.value( QStringLiteral( "software_version" ) ).toString();
        for ( const auto &rv : doc.value( QStringLiteral( "rows" ) ).toArray() )
        {
            const QJsonObject o = rv.toObject();
            sicnu::teaching_admin::BatchRowResult row;
            row.studentId = o.value( QStringLiteral( "student_id" ) ).toString();
            row.labId = o.value( QStringLiteral( "lab_id" ) ).toString();
            row.status = o.value( QStringLiteral( "status" ) ).toString();
            row.score = o.value( QStringLiteral( "score" ) ).toDouble( -1 );
            row.verdict = o.value( QStringLiteral( "verdict" ) ).toString();
            row.message = o.value( QStringLiteral( "message" ) ).toString();
            row.missingEvidence = o.value( QStringLiteral( "missing_evidence" ) ).toBool();
            row.rubricVersion = o.value( QStringLiteral( "rubric_version" ) ).toString();
            row.labVersion = o.value( QStringLiteral( "lab_version" ) ).toString();
            row.softwareVersion = o.value( QStringLiteral( "software_version" ) ).toString();
            report.rows.push_back( row );
        }
        appendLog( QString::fromUtf8( QJsonDocument(
          sicnu::teaching_admin::buildClassSummary( report ) ).toJson( QJsonDocument::Compact ) ) );
    }
}

void TeachingAdminDock::onBuildFeedbackAndSummary()
{
    QFile f( m_batchOutEdit->text() + QStringLiteral( ".json" ) );
    if ( !f.open( QIODevice::ReadOnly ) )
    {
        appendLog( tr( "no batch json at %1" ).arg( f.fileName() ) );
        return;
    }
    const auto doc = QJsonDocument::fromJson( f.readAll() ).object();
    sicnu::teaching_admin::BatchAssessmentReport report;
    report.labId = doc.value( QStringLiteral( "lab_id" ) ).toString();
    report.total = doc.value( QStringLiteral( "total" ) ).toInt();
    report.rubricVersion = doc.value( QStringLiteral( "rubric_version" ) ).toString();
    report.labVersion = doc.value( QStringLiteral( "lab_version" ) ).toString();
    report.softwareVersion = doc.value( QStringLiteral( "software_version" ) ).toString();
    for ( const auto &rv : doc.value( QStringLiteral( "rows" ) ).toArray() )
    {
        const QJsonObject o = rv.toObject();
        sicnu::teaching_admin::BatchRowResult row;
        row.studentId = o.value( QStringLiteral( "student_id" ) ).toString();
        row.labId = o.value( QStringLiteral( "lab_id" ) ).toString();
        row.status = o.value( QStringLiteral( "status" ) ).toString();
        row.score = o.value( QStringLiteral( "score" ) ).toDouble( -1 );
        row.verdict = o.value( QStringLiteral( "verdict" ) ).toString();
        row.message = o.value( QStringLiteral( "message" ) ).toString();
        row.missingEvidence = o.value( QStringLiteral( "missing_evidence" ) ).toBool();
        row.rubricVersion = o.value( QStringLiteral( "rubric_version" ) ).toString();
        row.labVersion = o.value( QStringLiteral( "lab_version" ) ).toString();
        row.softwareVersion = o.value( QStringLiteral( "software_version" ) ).toString();
        report.rows.push_back( row );

        sicnu::teaching_admin::FeedbackPackInput fin;
        fin.row = row;
        const auto fb = sicnu::teaching_admin::buildFeedbackPack( fin );
        appendLog( QString::fromUtf8( QJsonDocument( fb ).toJson( QJsonDocument::Compact ) ) );
    }
    appendLog( QString::fromUtf8(
        QJsonDocument( sicnu::teaching_admin::buildClassSummary( report ) ).toJson( QJsonDocument::Indented ) ) );
}

} // namespace sicnu::app::teaching_admin
