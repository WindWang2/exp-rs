#include "teaching_admin_dock.h"

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/class_summary.h"
#include "teaching_admin/curriculum_editor.h"
#include "teaching_admin/data_pack_manager.h"
#include "teaching_admin/feedback_pack.h"
#include "teaching_admin/labspec_authoring.h"
#include "teaching_admin/release_preflight.h"
#include "teaching_admin/rubric_builder.h"
#include "teaching_admin/script_adapters.h"

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

namespace sicnu::app::teaching_admin {

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
        m_operatorsEdit = new QLineEdit( QStringLiteral( "rs:extract_bands,rs:resample,rs:ndvi" ) );
        auto *row = new QHBoxLayout;
        row->addWidget( new QLabel( tr( "known operators" ) ) );
        row->addWidget( m_operatorsEdit, 1 );
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
        m_labIdEdit = new QLineEdit( QStringLiteral( "lab15_data_inspection" ) );
        m_batchOutEdit = new QLineEdit( QDir::temp().filePath( QStringLiteral( "teaching_admin_batch" ) ) );
        lay->addRow( tr( "submissions dir" ), m_submissionsDirEdit );
        lay->addRow( tr( "lab id" ), m_labIdEdit );
        lay->addRow( tr( "out prefix" ), m_batchOutEdit );
        auto *btn = new QPushButton( tr( "Batch Grade (local mock-capable)" ) );
        connect( btn, &QPushButton::clicked, this, &TeachingAdminDock::onRunBatch );
        lay->addRow( btn );
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
    m_log->setReadOnly( true );
    m_log->setMaximumBlockCount( 2000 );
    root->addWidget( new QLabel( tr( "Console" ) ) );
    root->addWidget( m_log, 1 );
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
    QSet<QString> ops;
    for ( const QString &o : m_operatorsEdit->text().split( QLatin1Char( ',' ), Qt::SkipEmptyParts ) )
        ops.insert( o.trimmed() );
    const auto vr = sicnu::teaching_admin::validateLabSpec( doc.object(), ops );
    appendLog( QString::fromUtf8( QJsonDocument( vr.toJson() ).toJson( QJsonDocument::Compact ) ) );
    const auto recipe = sicnu::teaching_admin::projectRecipeCompileView( doc.object() );
    appendLog( QString::fromUtf8( QJsonDocument( recipe ).toJson( QJsonDocument::Compact ) ) );
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
    QSet<QString> ops;
    for ( const QString &o : m_operatorsEdit->text().split( QLatin1Char( ',' ), Qt::SkipEmptyParts ) )
        ops.insert( o.trimmed() );
    in.knownOperators = ops;
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
    BatchAssessmentConfig cfg;
    cfg.labId = m_labIdEdit->text();
    cfg.submissionsDir = m_submissionsDirEdit->text();
    cfg.outDir = QFileInfo( m_batchOutEdit->text() ).path();
    cfg.rubricVersion = QStringLiteral( "ui-1" );
    cfg.labVersion = QStringLiteral( "ui-1" );
    cfg.softwareVersion = m_softwareVersionEdit->text();

    std::atomic<bool> cancel{ false };
    const auto report = runBatchAssessment(
        cfg,
        [&]( const SubmissionItem &item ) {
            BatchRowResult row;
            row.studentId = item.studentId;
            row.labId = cfg.labId;
            row.artifactPath = item.path;
            QFile f( item.path );
            if ( !f.open( QIODevice::ReadOnly ) )
            {
                row.status = QStringLiteral( "corrupted" );
                row.verdict = QStringLiteral( "error" );
                row.message = QStringLiteral( "cannot open submission" );
                return row;
            }
            const QByteArray bytes = f.readAll();
            if ( bytes.isEmpty() )
            {
                row.status = QStringLiteral( "corrupted" );
                row.verdict = QStringLiteral( "error" );
                row.message = QStringLiteral( "empty submission" );
                return row;
            }
            if ( bytes.startsWith( "CORRUPT" ) )
            {
                row.status = QStringLiteral( "corrupted" );
                row.verdict = QStringLiteral( "error" );
                row.message = QStringLiteral( "corrupt marker" );
                return row;
            }
            if ( bytes.startsWith( "MISSING_EVIDENCE" ) )
            {
                row.status = QStringLiteral( "unavailable" );
                row.verdict = QStringLiteral( "unavailable" );
                row.missingEvidence = true;
                row.score = -1;
                row.message = QStringLiteral( "missing evidence — not a silent zero" );
                return row;
            }
            row.status = QStringLiteral( "pass" );
            row.verdict = QStringLiteral( "pass" );
            row.score = 80.0;
            row.message = QStringLiteral( "local dry-run grade" );
            return row;
        },
        &cancel );

    publishBatchOutputsAtomic( report, m_batchOutEdit->text() );
    appendLog( QString::fromUtf8( QJsonDocument( report.toJson() ).toJson( QJsonDocument::Compact ) ) );
    appendLog( QString::fromUtf8(
        QJsonDocument( buildClassSummary( report ) ).toJson( QJsonDocument::Compact ) ) );
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
