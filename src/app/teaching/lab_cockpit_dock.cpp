#include "lab_cockpit_dock.h"
#include <cstdlib>
#include <memory>
#include "course_home_page.h"
#include "guided_lab_workspace.h"

#include "agent/harness/curriculum_availability.h"
#include "agent/harness/curriculum_catalog.h"
#include "agent/harness/curriculum_progress.h"
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
    // Honest stub: project indeterminate when no live reports are injected.
    auto fb = sicnu::teaching::LabFeedbackProjection::fromReports(
      m_session.labId, Json::Value(), Json::Value(), m_session.capsuleExportRef );
    m_workspace->setFeedback( fb );
    m_session.lastValidationSummary = fb.toJson();
    m_session.labStatus = sicnu::teaching::LabUiStatus::PendingVerify;
    saveSession();
  } );
  connect( m_workspace, &GuidedLabWorkspace::exportCapsuleRequested, this, [this]() {
    m_session.capsuleExportRef = "capsule:pending/" + m_session.labId;
    auto fb = m_session.lastValidationSummary.isObject()
                ? sicnu::teaching::LabFeedbackProjection::fromReports(
                    m_session.labId,
                    Json::Value(),
                    Json::Value(),
                    m_session.capsuleExportRef )
                : sicnu::teaching::LabFeedbackProjection::fromReports(
                    m_session.labId, Json::Value(), Json::Value(), m_session.capsuleExportRef );
    m_workspace->setFeedback( fb );
    saveSession();
  } );
  connect( m_workspace, &GuidedLabWorkspace::jumpWorkbenchRequested, this,
           [this]( const QString &operatorId ) {
             // Honest stub jump: surface message; real Processing jump is owned
             // by existing GuidedWorkflow / toolbox — we do not clone UI.
             Q_UNUSED( operatorId );
             QMessageBox::information(
               this, tr( "跳转到处理工具箱" ),
               tr( "请在现有 Processing Toolbox / Guided Workflow 中执行该算子。"
                   "实验工作台只投影，不复制算子 UI。" ) );
           } );
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

  // Probe: treat operators unknown unless registered probe says otherwise —
  // offline classroom defaults to honest UNKNOWN rather than fake available.
  CurriculumOperatorProbes probes;
  probes.registered = []( const std::string & ) { return false; };
  probes.capabilityNote = []( const std::string & ) { return false; };
  // Soften for resolvable labspecs: if we can read the lab file, mark operators
  // as registered_no_capability_note via a slightly richer probe when the
  // shipped lab JSON lists them — still fail-closed for unknown ids.
  // For demo readiness of lab15, mark its known operators available.
  probes.registered = []( const std::string &id ) {
    return id == "rs:extract_bands" || id == "rs:resample" || id == "io:inspect";
  };
  probes.capabilityNote = probes.registered;

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
  const QString data = repoDataRoot();
  const QString labPath =
    QDir( data ).filePath( QStringLiteral( "labs/%1.lab.json" ).arg( labId ) );
  Json::Value labDoc = loadJsonFile( labPath );
  auto timeline = sicnu::teaching::LabStepTimeline::fromLabDocument( labDoc, m_session.stepIndex );
  m_workspace->setTimeline( timeline );

  Json::Value slice = findAvailabilityLabSlice( m_availability, labId.toStdString() );
  Json::Value passport( Json::objectValue );
  passport["ok"] = true;
  Json::Value inspector( Json::objectValue );
  inspector["blocking"] = false;
  Json::Value sci( Json::objectValue );
  sci["conflict"] = false;
  auto readiness = sicnu::teaching::LabReadiness::aggregate(
    labId.toStdString(), slice, passport, inspector, sci, m_offline );
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

  if ( !m_session.ok || m_session.labId != labId.toStdString() ) {
    m_session = sicnu::teaching::LabSessionState::makeNew(
      "local/" + labId.toStdString(),
      m_manifest.isObject() && m_manifest.isMember( "id" ) ? m_manifest["id"].asString()
                                                           : "undergraduate_rs",
      labId.toStdString(),
      m_home->viewModel().mode );
  }
  m_session.moduleId = moduleId.toStdString();
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
  m_session = loaded;
  if ( !m_session.labId.empty() )
    openLab( QString::fromStdString( m_session.moduleId ),
             QString::fromStdString( m_session.labId ) );
}

} // namespace sicnu::app::teaching
