#include "lab_cockpit_dock.h"
#include <cstdlib>
#include <memory>
#include "course_home_page.h"
#include "guided_lab_workspace.h"
#include "lab_validate_service.h"

#include "agent/harness/curriculum_availability.h"
#include "agent/harness/curriculum_catalog.h"
#include "agent/harness/curriculum_progress.h"
#include "agent/harness/curriculum_registry_probe.h"
#include "dataset/dataset_store.h"
#include "experiment/capsule/capsule_builder.h"
#include "experiment/capsule/capsule_io.h"
#include "experiment/experiment_store.h"
#include "agent/autonomy/autonomy_policy.h"
#include "agent/autonomy/autonomy_projection.h"
#include "teaching/autonomy_effective_display.h"
#include "teaching/course_home_view_model.h"
#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_readiness.h"
#include "teaching/lab_step_timeline.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <fstream>
#include <sstream>

// Forward-declare main window slots we may call without pulling the full header
// in unit-test builds; in the app we include main_window.h from the cpp TU
// that registers the dock.
class QgisDesktopWindow;

namespace sicnu::app::teaching {
namespace {

QString repoDataRoot()
{
  // Prefer SICNU_SOURCE_DIR / SICNU_DATA_DIR, else walk up from cwd.
  if ( const char *d = std::getenv( "SICNU_DATA_DIR" ) ) {
    if ( d[0] ) return QString::fromUtf8( d );
  }
  if ( const char *s = std::getenv( "SICNU_SOURCE_DIR" ) ) {
    if ( s[0] ) return QString::fromUtf8( s ) + QStringLiteral( "/data" );
  }
  QDir dir = QDir::current();
  for ( int i = 0; i < 8; ++i ) {
    if ( dir.exists( QStringLiteral( "data/curriculum" ) ) )
      return dir.filePath( QStringLiteral( "data" ) );
    if ( !dir.cdUp() ) break;
  }
  return QStringLiteral( "data" );
}

} // namespace

LabCockpitDock::LabCockpitDock( QgisDesktopWindow *mainWindow, QWidget *parent )
  : QWidget( parent )
  , m_mainWindow( mainWindow )
{
  setObjectName( QStringLiteral( "undergradLabCockpit" ) );
  auto *lay = new QVBoxLayout( this );
  m_stack = new QStackedWidget( this );
  m_home = new CourseHomePage( this );
  m_workspace = new GuidedLabWorkspace( this );
  m_stack->addWidget( m_home );
  m_stack->addWidget( m_workspace );
  lay->addWidget( m_stack );
  wireSignals();
  loadCourseFromRepo();
  restoreSession();
}

void LabCockpitDock::setOperatorLauncher( OperatorLauncher launcher )
{
  m_launchOperator = std::move( launcher );
}

void LabCockpitDock::setCapsuleSourceProvider( CapsuleSourceProvider provider )
{
  m_capsuleSource = std::move( provider );
}

void LabCockpitDock::wireSignals()
{
  connect( m_home, &CourseHomePage::labSelected, this, &LabCockpitDock::openLab );
  connect( m_home, &CourseHomePage::continueRequested, this, &LabCockpitDock::openLab );
  connect( m_home, &CourseHomePage::modeChanged, this, [this]( const QString &mode ) {
    sicnu::teaching::ExperienceMode m;
    if ( sicnu::teaching::experienceModeFromWire( mode.toStdString(), m ) ) {
      auto vm = m_home->viewModel();
      // Rebuild with same docs, new mode — same truth sources.
      auto rebuilt = sicnu::teaching::CourseHomeViewModel::fromDocuments(
        m_manifest, m_progressSummary, m_availability, m );
      m_home->setViewModel( rebuilt );
      m_session.mode = m;
      saveSession();
    }
  } );
  connect( m_workspace, &GuidedLabWorkspace::stepIndexChanged, this, [this]( int idx ) {
    m_session.stepIndex = idx;
    saveSession();
  } );
  connect( m_workspace, &GuidedLabWorkspace::humanEvidenceSubmitted, this,
           [this]( const QString &stepId, const QString &text ) {
             m_session.evidenceRefs.push_back( ( "human:" + stepId + ":" + QString::number( text.size() ) )
                                                 .toStdString() );
             saveSession();
           } );
  connect( m_workspace, &GuidedLabWorkspace::validateRequested, this, [this]() {
    // Real wiring: the same OutputVerifier engines `lab --grade` wraps,
    // projected through the teaching leaf. Empty artifact → honest
    // indeterminate; the UI never recomputes a verdict.
    m_session.artifactPath = m_workspace->artifactPath().toStdString();
    sicnu::app::teaching::LabValidateInput in;
    in.labId = m_session.labId;
    in.artifactPath = m_session.artifactPath;
    in.rulesPath = sicnu::app::teaching::resolveLabRulesPath(
      m_labDoc, repoDataRoot().toStdString() );
    auto fb = sicnu::app::teaching::projectValidation( in );
    m_workspace->setFeedback( fb );
    m_session.lastValidationSummary = fb.toJson();
    m_session.labStatus = fb.overallStatus == "pass" ? sicnu::teaching::LabUiStatus::Completed
                          : fb.overallStatus == "fail"
                            ? sicnu::teaching::LabUiStatus::NeedsCorrection
                            : sicnu::teaching::LabUiStatus::PendingVerify;
    saveSession();
  } );
  connect( m_workspace, &GuidedLabWorkspace::runOperatorRequested, this,
           [this]( const QString &operatorId, const QString &paramsJson ) {
             Q_UNUSED( paramsJson ); // the Processing dialog owns parameter widgets
             launchOperator( operatorId );
           } );
  connect( m_workspace, &GuidedLabWorkspace::jumpWorkbenchRequested, this,
           [this]( const QString &operatorId ) { launchOperator( operatorId ); } );
  connect( m_workspace, &GuidedLabWorkspace::exportCapsuleRequested, this,
           [this]() { exportCapsule(); } );
}

void LabCockpitDock::launchOperator( const QString &operatorId )
{
  // Hand off to the EXISTING Processing surface (openProcessingAlgorithm);
  // we never clone the operator UI and never report a fake launch.
  if ( m_launchOperator ) {
    m_launchOperator( operatorId );
    return;
  }
  QMessageBox::information(
    this, tr( "跳转到处理工具箱" ),
    tr( "请在现有 Processing Toolbox / Guided Workflow 中执行算子 %1。"
        "实验工作台只投影，不复制算子 UI。" )
      .arg( operatorId ) );
}

void LabCockpitDock::appendExportNote( const QString &noteZh )
{
  m_workspace->appendFeedbackNote( noteZh );
}

void LabCockpitDock::exportCapsule()
{
  namespace caps = sicnu::experiment::capsule;
  const LabCapsuleSource src = m_capsuleSource ? m_capsuleSource() : LabCapsuleSource{};
  auto fail = [this]( const QString &reasonZh ) {
    // Never leave or fabricate a capsule ref after a failed export.
    m_session.capsuleExportRef.clear();
    appendExportNote( tr( "导出失败: %1" ).arg( reasonZh ) );
    saveSession();
  };
  auto diagText = []( const auto &result ) -> QString {
    return result.diagnostics().isEmpty() ? QString()
                                          : result.diagnostics().first().message;
  };

  if ( src.experimentDbPath.isEmpty() ) {
    fail( tr( "未打开实验记录库：没有可提交的运行记录（不做假引用）" ) );
    return;
  }
  sicnu::experiment::ExperimentStore store;
  QString err;
  if ( !store.open( src.experimentDbPath, &err ) ) {
    fail( tr( "实验记录库打开失败: %1" ).arg( err ) );
    return;
  }

  if ( m_session.runId.empty() ) {
    // Bind deterministically to recorded truth (same policy as the classroom
    // CLI): the newest run recorded for THIS experiment — never a guess.
    const QString expId = !src.experimentId.isEmpty()
                            ? src.experimentId
                            : QString::fromStdString( m_session.experimentId );
    if ( expId.isEmpty() ) {
      fail( tr( "无法确定所属实验：未打开 lab 项目（不做跨实验猜测绑定）" ) );
      return;
    }
    auto totalRes = store.listRuns( expId, QString(), QString(), 0, 1 );
    if ( !totalRes ) {
      fail( tr( "无法读取运行记录: %1" ).arg( diagText( totalRes ) ) );
      return;
    }
    const qint64 total = totalRes.value().first;
    if ( total <= 0 ) {
      fail( tr( "实验还没有已记录的运行（先在处理工具箱完成运行并保存）" ) );
      return;
    }
    auto lastRes = store.listRuns( expId, QString(), QString(), total - 1, 1 );
    if ( !lastRes || lastRes.value().second.isEmpty() ) {
      fail( tr( "无法读取最新运行记录" ) );
      return;
    }
    m_session.runId = lastRes.value().second.first().runId().toStdString();
    appendExportNote( tr( "已绑定最新记录的运行: %1" )
                        .arg( QString::fromStdString( m_session.runId ) ) );
  }

  sicnu::dataset::DatasetStore datasets;
  if ( !src.datasetDbPath.isEmpty() ) {
    QString dsErr;
    if ( !datasets.open( src.datasetDbPath, &dsErr ) )
      appendExportNote( tr( "数据集库不可用（%1）：数据集版本将按未解析记录" ).arg( dsErr ) );
  }

  caps::CapsuleBuilder builder( store, datasets );
  caps::CapsuleOptions opts;
  opts.workspaceRoot = src.workspaceRoot;
  caps::CapsuleHooks hooks; // unwired ⇒ recorded facts only, never fabricated

  const QString runId = QString::fromStdString( m_session.runId );
  auto built = builder.build( runId, opts, hooks );
  if ( !built ) {
    // A pruned/deleted run must not wedge the session forever: drop the
    // stale binding so the next export re-derives the newest recorded run
    // by the same deterministic policy (still no guessing).
    const bool runMissing = !built.diagnostics().isEmpty()
                            && built.diagnostics().first().code
                                 == QStringLiteral( "capsule.run-missing" );
    if ( runMissing ) {
      m_session.runId.clear();
      appendExportNote( tr( "已记录的运行 %1 已不存在，下次导出将重新绑定最新运行" ).arg( runId ) );
    }
    fail( tr( "胶囊构建失败（%1）: %2" )
            .arg( runId, diagText( built ) ) );
    return;
  }

  const QString exportDir =
    QStandardPaths::writableLocation( QStandardPaths::AppDataLocation )
    + QStringLiteral( "/teaching/exports" );
  QDir().mkpath( exportDir );
  const QString path = exportDir + QStringLiteral( "/%1.capsule.json" ).arg( runId );
  auto exported = caps::CapsuleIO::exportCapsule( built.value(), path );
  if ( !exported ) {
    fail( tr( "胶囊写入失败: %1" ).arg( diagText( exported ) ) );
    return;
  }
  m_session.capsuleExportRef =
    ( QStringLiteral( "file:" ) + exported.value().path ).toStdString();
  appendExportNote( tr( "胶囊已导出: %1（%2 字节）" )
                      .arg( exported.value().path )
                      .arg( exported.value().bytes ) );
  saveSession();
}

Json::Value LabCockpitDock::loadJsonFile( const QString &path ) const
{
  QFile f( path );
  if ( !f.open( QIODevice::ReadOnly ) ) return Json::Value();
  const QByteArray bytes = f.readAll();
  Json::Value root;
  Json::CharReaderBuilder b;
  std::string errs;
  std::unique_ptr<Json::CharReader> reader( b.newCharReader() );
  if ( !reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &root, &errs ) )
    return Json::Value();
  return root;
}

Json::Value LabCockpitDock::findAvailabilityLabSlice( const Json::Value &availability,
                                                      const std::string &labId ) const
{
  if ( !availability.isObject() ) return Json::Value();
  const auto &modules = availability["modules"];
  if ( !modules.isArray() ) return Json::Value();
  for ( const auto &mod : modules ) {
    const auto &labs = mod["labs"];
    if ( !labs.isArray() ) continue;
    for ( const auto &lab : labs ) {
      if ( lab.isObject() && lab.isMember( "lab_id" ) && lab["lab_id"].isString()
           && lab["lab_id"].asString() == labId )
        return lab;
    }
  }
  return Json::Value();
}

void LabCockpitDock::loadCourseFromRepo()
{
  using namespace sicnu::agent::harness;
  const QString data = repoDataRoot();
  const QString curriculumPath =
    QDir( data ).filePath( QStringLiteral( "curriculum/undergraduate_rs.curriculum.json" ) );
  m_manifest = loadJsonFile( curriculumPath );

  CurriculumPaths paths;
  paths.labsDir = QDir( data ).filePath( QStringLiteral( "labs" ) ).toStdString();
  paths.packsDir = QDir( data ).filePath( QStringLiteral( "labs/packs" ) ).toStdString();

  // Probe: operators are unknown unless the authoritative registry probe
  // (RSOperatorRegistry::hasOperator + CapabilityKnowledge, sicnu_agent)
  // says otherwise — the offline classroom stays honest UNKNOWN, fail-closed.
  // No hard-coded operator id list lives here; the runtime registry is the
  // single truth source.
  const CurriculumOperatorProbes probes = defaultCurriculumProbes();

  if ( m_manifest.isObject() ) {
    m_availability = buildAvailabilityReport( m_manifest, paths.labsDir, paths.packsDir, probes );
  }

  Json::Value progress = CurriculumProgress::emptyDoc();
  m_progressSummary = CurriculumProgress::summary( progress, m_manifest, {} );

  auto vm = sicnu::teaching::CourseHomeViewModel::fromDocuments(
    m_manifest, m_progressSummary, m_availability, sicnu::teaching::ExperienceMode::Beginner );
  // Enrich titles from lab files when present.
  for ( auto &mc : vm.modules ) {
    for ( auto &lc : mc.labs ) {
      const QString labPath =
        QDir( data ).filePath( QStringLiteral( "labs/%1.lab.json" )
                                 .arg( QString::fromStdString( lc.labId ) ) );
      const Json::Value lab = loadJsonFile( labPath );
      if ( lab.isObject() ) {
        if ( lab.isMember( "title_zh" ) && lab["title_zh"].isString() )
          lc.titleZh = lab["title_zh"].asString();
        else if ( lab.isMember( "title" ) && lab["title"].isString() )
          lc.titleZh = lab["title"].asString();
      }
    }
  }
  m_home->setViewModel( vm );
  m_stack->setCurrentWidget( m_home );
}

void LabCockpitDock::showCourseHome()
{
  m_stack->setCurrentWidget( m_home );
}

void LabCockpitDock::openLab( const QString &moduleId, const QString &labId )
{
  // Session boundary FIRST: switching lab (or arriving fresh) resets step
  // navigation so the previous lab's step index never leaks into this
  // timeline.
  if ( !m_session.ok || m_session.labId != labId.toStdString() ) {
    m_session = sicnu::teaching::LabSessionState::makeNew(
      "local/" + labId.toStdString(),
      m_manifest.isObject() && m_manifest.isMember( "id" ) ? m_manifest["id"].asString()
                                                           : "undergraduate_rs",
      labId.toStdString(),
      m_home->viewModel().mode );
    m_session.artifactPath.clear();
    m_workspace->setArtifactPath( QString() );
    m_workspace->clearFeedback();
  }
  m_session.moduleId = moduleId.toStdString();

  const QString data = repoDataRoot();
  const QString labPath =
    QDir( data ).filePath( QStringLiteral( "labs/%1.lab.json" ).arg( labId ) );
  Json::Value labDoc = loadJsonFile( labPath );
  m_labDoc = labDoc;
  auto timeline = sicnu::teaching::LabStepTimeline::fromLabDocument( labDoc, m_session.stepIndex );
  m_workspace->setTimeline( timeline );

  // Honest readiness: passport / inspector / scientific facts are NOT
  // fabricated here. Without an injected preflight the aggregate records
  // UNKNOWN items (fail-closed) instead of a fake OK.
  Json::Value slice = findAvailabilityLabSlice( m_availability, labId.toStdString() );
  auto readiness = sicnu::teaching::LabReadiness::aggregate(
    labId.toStdString(), slice, Json::Value(), Json::Value(), Json::Value(), m_offline );
  m_workspace->setReadiness( readiness );

  Json::Value policy( Json::objectValue );
  policy["schema"] = "sicnu.autonomy-policy/1";
  policy["level"] = "L2";
  policy["mode"] = "practice";
  auto autonomy = sicnu::teaching::AutonomyEffectiveDisplay::fromPolicyDoc( policy, "student", "lab" );
  m_workspace->setAutonomy( autonomy );

  QString why = tr( "（可解释工作流投影）\n" );
  if ( const auto *cur = timeline.current() ) {
    why += QString::fromStdString( cur->whyHintZh );
    why += QLatin1Char( '\n' );
    why += tr( "\n来源徽章: 系统事实 | 编写指引 | 推断\n" );
  }
  m_workspace->setWhyMarkdown( why );

  // Restore the persisted feedback summary for THIS lab only; a summary from
  // another lab is never shown. The summary is re-materialized from the
  // recorded projection — no verdict is recomputed here.
  const auto &summary = m_session.lastValidationSummary;
  if ( summary.isObject() && summary.isMember( "schema" ) ) {
    auto fb = sicnu::teaching::LabFeedbackProjection::fromJson( summary );
    if ( fb.ok && fb.labId == m_session.labId ) {
      // A successful export after the last validation is recorded in the
      // session, not in the summary — overlay it so the ref survives restart.
      if ( !m_session.capsuleExportRef.empty() )
        fb.capsuleExportRef = m_session.capsuleExportRef;
      m_workspace->setFeedback( fb );
    }
  }
  m_session.autonomyPolicyRef = "inline:practice/L2";
  saveSession();

  m_stack->setCurrentWidget( m_workspace );
}

QString LabCockpitDock::sessionPath() const
{
  const QString root =
    QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
  QDir().mkpath( root + QStringLiteral( "/teaching" ) );
  return root + QStringLiteral( "/teaching/lab_cockpit_session.json" );
}

bool LabCockpitDock::saveSession()
{
  if ( !m_session.ok ) return false;
  return m_session.saveToFile( sessionPath().toStdString() );
}

void LabCockpitDock::restoreSession()
{
  auto loaded = sicnu::teaching::LabSessionState::loadFromFile( sessionPath().toStdString() );
  if ( !loaded.ok ) return; // fail-closed: stay on course home
  // Course-switch fail-closed: a session recorded under another course must
  // not silently adopt this manifest's labs. Drop to a fresh state on the
  // Course Home instead.
  const std::string manifestId =
    m_manifest.isObject() && m_manifest.isMember( "id" ) && m_manifest["id"].isString()
      ? m_manifest["id"].asString()
      : "undergraduate_rs";
  if ( loaded.courseId != manifestId ) {
    m_session = sicnu::teaching::LabSessionState{};
    return;
  }
  m_session = loaded;
  m_workspace->setArtifactPath( QString::fromStdString( m_session.artifactPath ) );
  if ( !m_session.labId.empty() )
    openLab( QString::fromStdString( m_session.moduleId ),
             QString::fromStdString( m_session.labId ) );
}

} // namespace sicnu::app::teaching
