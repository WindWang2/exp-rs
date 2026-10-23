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
#include <QTabWidget>
#include <QVBoxLayout>
#include <atomic>
#include <stdexcept>

namespace sicnu::app::teaching_admin {

TeachingAdminDock::~TeachingAdminDock()
{
    // A destroyed console must not leave a batch thread touching `this`.
    m_batchCancel.store( true );
    if ( m_batchThread.joinable() )
        m_batchThread.join();
    std::lock_guard<std::mutex> lock( m_retiredMutex );
    for ( auto &r : m_retiredThreads )
    {
        if ( r.thread.joinable() )
            r.thread.join();
    }
    m_retiredThreads.clear();
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
        m_tabs->addTab( page, tr( "A Courses" ) );
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
        m_tabs->addTab( page, tr( "B Experiments" ) );
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
        m_tabs->addTab( page, tr( "C Rubrics" ) );
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
        m_tabs->addTab( page, tr( "D Data packs" ) );
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
        m_tabs->addTab( page, tr( "E Preflight" ) );
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
        m_tabs->addTab( page, tr( "F Offline packs" ) );
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
        m_batchRunButton = btn;
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
        m_tabs->addTab( page, tr( "G Batch grading" ) );
    }

    // --- H/I Feedback + Summary ---
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout( page );
        auto *btn = new QPushButton( tr( "Build Feedback Packs + Class Summary from last batch out" ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onBuildFeedbackAndSummary );
        lay->addWidget( btn );
        lay->addStretch( 1 );
        m_tabs->addTab( page, tr( "H/I Feedback & stats" ) );
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

    // Arm the console state honestly for the run's duration (#1453/WP02):
    // the run affordance and the out-prefix edit go inert — the batch reads
    // the prefix once at start, so a mid-run edit must not be able to point
    // the class summary at a different file than the one being written.
    m_batchRunButton->setEnabled( false );
    m_batchOutEdit->setEnabled( false );

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

    QVector<sicnu::teaching_admin::DiscoverySkip> discoverySkips;
    const int total =
      sicnu::teaching_admin::discoverSubmissions( cfg.submissionsDir, &discoverySkips ).size();
    m_batchProgressLabel->setText( tr( "running 0 / %1" ).arg( total ) );
    if ( !discoverySkips.isEmpty() )
    {
        // Preview honesty (#1466): refusals are visible BEFORE the run, so
        // a mis-named, hidden or misplaced submission can be fixed while
        // grading. The refusal classes differ (reserved output names →
        // rename; hidden entries → clear the attribute; nested directories
        // → move the file up one level), so the preview names the entries
        // and defers the per-entry remediation to the report's reasons.
        QStringList names;
        for ( const auto &skip : discoverySkips )
            names << QFileInfo( skip.path ).fileName();
        appendLog( tr( "discovery refused %1 entr%2 (skipped, not graded): %3 — "
                       "see the report for each refusal's reason and remedy" )
                     .arg( discoverySkips.size() )
                     .arg( discoverySkips.size() == 1 ? QStringLiteral( "y" )
                                                      : QStringLiteral( "ies" ) )
                     .arg( names.join( QStringLiteral( ", " ) ) ) );
    }
    m_batchCancelButton->setEnabled( true );

    const QString outPrefix = m_batchOutEdit->text();
    // Spawn barrier (review finding): a throwing std::thread constructor
    // (resource exhaustion) must re-arm the console — otherwise no worker
    // ever posts a completion callback and the run button stays disabled
    // forever. The catch is non-const by std::thread's copy requirement.
    try
    {
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
                // A publish failure must be a TYPED failure, never a silent
                // fake success (#1453/WP02): routing it through the same catch
                // makes the console fail visibly and re-arm.
                if ( !publishBatchOutputsAtomic( report, outPrefix ) )
                    throw std::runtime_error( "publishing batch outputs failed (unwritable out prefix)" );
                QMetaObject::invokeMethod(
                  this,
                  [this, report, outPrefix]() { finishBatchOnUiThread( report.toJson(), outPrefix ); },
                  Qt::QueuedConnection );
            }
            catch ( const std::exception &e )
            {
                const QString what = QString::fromUtf8( e.what() );
                QMetaObject::invokeMethod(
                  this, [this, what]() { failBatchOnUiThread( what ); }, Qt::QueuedConnection );
            }
            catch ( ... )
            {
                QMetaObject::invokeMethod(
                  this, [this]() { failBatchOnUiThread( QString() ); }, Qt::QueuedConnection );
            }
        } );
    }
    catch ( ... )
    {
        m_batchCancelButton->setEnabled( false );
        m_batchRunButton->setEnabled( true );
        m_batchOutEdit->setEnabled( true );
        m_batchProgressLabel->setText( tr( "batch failed (could not start worker thread)" ) );
        appendLog( tr( "batch failed (could not start worker thread)" ) );
    }
}

void TeachingAdminDock::onCancelBatch()
{
    // Typed cooperation, not a kill: the orchestrator finishes the item in
    // flight, keeps completed rows, and types the remainder cancelled.
    m_batchCancel.store( true );
    m_batchCancelButton->setEnabled( false );
    m_batchProgressLabel->setText( m_batchProgressLabel->text() + tr( " — cancelling…" ) );
}



void TeachingAdminDock::pruneRetiredThreadsLocked()
{
    for ( auto it = m_retiredThreads.begin(); it != m_retiredThreads.end(); )
    {
        if ( it->done && it->done->load( std::memory_order_acquire ) )
        {
            if ( it->thread.joinable() )
                it->thread.join();
            it = m_retiredThreads.erase( it );
        }
        else
        {
            ++it;
        }
    }
}

void TeachingAdminDock::finishBatchOnUiThread( QJsonObject reportJson, const QString &outPrefix )
{
    if ( m_batchThread.joinable() )
    {
        std::lock_guard<std::mutex> lock( m_retiredMutex );
        pruneRetiredThreadsLocked();
        auto done = std::make_shared<std::atomic<bool>>( false );
        std::thread reaper( [t = std::move( m_batchThread ), done]() mutable {
            if ( t.joinable() )
                t.join();
            done->store( true, std::memory_order_release );
        } );
        m_retiredThreads.push_back( { std::move( reaper ), done } );
    }
    // Re-arm the console for the next run (#1453/WP02): the run affordance
    // and the out-prefix edit were disabled for the run's duration.
    m_batchCancelButton->setEnabled( false );
    m_batchRunButton->setEnabled( true );
    m_batchOutEdit->setEnabled( true );
    const QString state =
      reportJson.value( QStringLiteral( "cancelled_early" ) ).toBool()
        ? tr( "cancelled (durable partial results published)" )
        : tr( "finished" );
    // Skipped refusals ride the summary (#1466): graded + skipped is the
    // full accounting of what discovery saw.
    QString summary =
      tr( "%1 — graded %2 / %3" )
        .arg( state )
        .arg( reportJson.value( QStringLiteral( "graded" ) ).toInt() )
        .arg( reportJson.value( QStringLiteral( "total" ) ).toInt() );
    const int skippedCount = reportJson.value( QStringLiteral( "skipped" ) ).toInt();
    if ( skippedCount > 0 )
        summary += tr( " — %1 file(s) skipped (reserved names, see report)" ).arg( skippedCount );
    m_batchProgressLabel->setText( summary );
    appendLog( QString::fromUtf8( QJsonDocument( reportJson ).toJson( QJsonDocument::Compact ) ) );

    // Class summary from the SAME published truth (the report on disk), not
    // from a parallel in-memory copy — read from the prefix the run STARTED
    // with, never from a (now impossible) mid-run edit of the field.
    // Fail-closed (restored from the r15-E fix lost in merge c8f5e0805422):
    // an unreadable/corrupt report is reported as such, never summarized as
    // "0 submissions, 0% completion".
    sicnu::teaching_admin::BatchAssessmentReport report;
    QString loadError;
    QString loadWarning;
    if ( !sicnu::teaching_admin::loadBatchReportFromFile( outPrefix + QStringLiteral( ".json" ),
                                                          report, loadError, &loadWarning ) )
    {
        appendLog( tr( "class summary skipped: %1" ).arg( loadError ) );
    }
    else
    {
        if ( !loadWarning.isEmpty() )
            appendLog( tr( "class summary warning: %1" ).arg( loadWarning ) );
        appendLog( QString::fromUtf8( QJsonDocument(
          sicnu::teaching_admin::buildClassSummary( report ) ).toJson( QJsonDocument::Compact ) ) );
    }
}

void TeachingAdminDock::failBatchOnUiThread( const QString &why )
{
    // The worker ran to completion (or died) on a typed failure: reap it and
    // re-arm the console without blocking GUI event loop.
    if ( m_batchThread.joinable() )
    {
        std::lock_guard<std::mutex> lock( m_retiredMutex );
        pruneRetiredThreadsLocked();
        auto done = std::make_shared<std::atomic<bool>>( false );
        std::thread reaper( [t = std::move( m_batchThread ), done]() mutable {
            if ( t.joinable() )
                t.join();
            done->store( true, std::memory_order_release );
        } );
        m_retiredThreads.push_back( { std::move( reaper ), done } );
    }
    m_batchCancelButton->setEnabled( false );
    m_batchRunButton->setEnabled( true );
    m_batchOutEdit->setEnabled( true );
    const QString text = why.isEmpty() ? tr( "batch failed (unknown error)" )
                                       : tr( "batch failed: %1" ).arg( why );
    m_batchProgressLabel->setText( text );
    appendLog( text );
}

void TeachingAdminDock::onBuildFeedbackAndSummary()
{
    // Fail-closed (restored from the r15-E fix lost in merge c8f5e0805422):
    // an unreadable, truncated or non-batch JSON file must not produce a
    // "0 submissions, 0% completion" summary that looks legitimate.
    sicnu::teaching_admin::BatchAssessmentReport report;
    QString loadError;
    QString loadWarning;
    const QString reportPath = m_batchOutEdit->text() + QStringLiteral( ".json" );
    if ( !sicnu::teaching_admin::loadBatchReportFromFile( reportPath, report, loadError, &loadWarning ) )
    {
        // loadError already carries the full path; prefixing it here would
        // print the same path twice.
        appendLog( tr( "no usable batch report: %1" ).arg( loadError ) );
        return;
    }
    if ( !loadWarning.isEmpty() )
        appendLog( tr( "batch report warning: %1" ).arg( loadWarning ) );

    for ( const sicnu::teaching_admin::BatchRowResult &row : report.rows )
    {
        sicnu::teaching_admin::FeedbackPackInput fin;
        fin.row = row;
        const auto fb = sicnu::teaching_admin::buildFeedbackPack( fin );
        appendLog( QString::fromUtf8( QJsonDocument( fb ).toJson( QJsonDocument::Compact ) ) );
    }
    appendLog( QString::fromUtf8(
        QJsonDocument( sicnu::teaching_admin::buildClassSummary( report ) ).toJson( QJsonDocument::Indented ) ) );
}

} // namespace sicnu::app::teaching_admin
