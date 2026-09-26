#include "lab_cockpit_dock.h"
#include <cstdlib>
#include <memory>
#include "course_home_page.h"
#include "guided_lab_workspace.h"
#include "lab_operator_launch.h"
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
#include <string_view>
#include <vector>
#include "platform/portable.h"

// Forward-declare main window slots we may call without pulling the full header
// in unit-test builds; in the app we include main_window.h from the cpp TU
// that registers the dock.
class QgisDesktopWindow;

namespace sicnu::app::teaching {
namespace {

QString repoDataRoot()
{
  // Prefer SICNU_SOURCE_DIR / SICNU_DATA_DIR, else walk up from cwd.
  {
    // Path-valued env vars: read through the UTF-8 boundary.
    const std::string d = sicnu::portable::envUtf8( "SICNU_DATA_DIR" );
    if ( !d.empty() ) return QString::fromStdString( d );
  }
  {
    const std::string s = sicnu::portable::envUtf8( "SICNU_SOURCE_DIR" );
    if ( !s.empty() ) return QString::fromStdString( s ) + QStringLiteral( "/data" );
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
             // Registry-checked prefill: the step's params pass the ONE
             // parameter validator before the operator surface may see them.
             // A refusal is surfaced as a feedback note — never silently
             // dropped, never auto-run.
             const auto plan = sicnu::app::teaching::prepareLabOperatorLaunch(
               operatorId, paramsJson );
             if ( !plan.ok ) {
               for ( const auto &issue : plan.issuesZh )
                 m_workspace->appendFeedbackNote(
                   QStringLiteral( "参数预填被拒绝: %1" ).arg(
                     QString::fromStdString( issue ) ) );
               return;
             }
             QString prefill;
             if ( plan.params.isObject() && !plan.params.empty() ) {
               Json::StreamWriterBuilder b;
               b["indentation"] = "";
               prefill = QString::fromStdString( Json::writeString(
                 b, sicnu::app::teaching::absolutizeLabInputPaths(
                      plan.params, repoDataRoot() ) ) );
             }
             launchOperator( operatorId, prefill );
           } );
  connect( m_workspace, &GuidedLabWorkspace::jumpWorkbenchRequested, this,
           [this]( const QString &operatorId ) { launchOperator( operatorId, QString() ); } );
  connect( m_workspace, &GuidedLabWorkspace::exportCapsuleRequested, this,
           [this]() { exportCapsule(); } );
}

void LabCockpitDock::onRecordingContextChanged()
{
  if ( !m_session.ok ) return;
  // The shell re-bound the recording context (project open/switch/close).
  // Artifact/run/capsule refs and validation summaries from the previous
  // workspace must not survive: a stale runId would bind the next export to
  // another project's experiment store once the ids collide, and a stale
  // summary shows a verdict for an artifact this workspace never produced.
  // Navigation progress (step index, evidence, mode) is course-scoped and kept.
  m_session.artifactPath.clear();
  m_session.runId.clear();
  m_session.capsuleExportRef.clear();
  m_session.lastValidationSummary = Json::Value();
  m_workspace->setArtifactPath( QString() );
  m_workspace->clearFeedback();
  appendExportNote( tr( "Project/record context switched: previous project artifacts, runs and capsule references cleared (no cross-project inheritance)" ) );
  saveSession();
}

void LabCockpitDock::launchOperator( const QString &operatorId, const QString &paramsJson )
{
  // Hand off to the EXISTING Processing surface (rs-operator task panel /
  // openProcessingAlgorithm); we never clone the operator UI and never
  // report a fake launch. paramsJson is prefill-only: the shell may fill the
  // operator's own parameter form with it, never auto-run.
  if ( m_launchOperator ) {
    m_launchOperator( operatorId, paramsJson );
    return;
  }
  QMessageBox::information(
    this, tr( "Jump to Processing Toolbox" ),
    tr( "Please run operator %1 in the existing Processing Toolbox / Guided Workflow (open the toolbox to run directly; Guided Workflow supports step-by-step execution)." )
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
    appendExportNote( tr( "Export failed: %1" ).arg( reasonZh ) );
    saveSession();
  };
  auto diagText = []( const auto &result ) -> QString {
    return result.diagnostics().isEmpty() ? QString()
                                          : result.diagnostics().first().message;
  };

  if ( src.experimentDbPath.isEmpty() ) {
    fail( tr( "Experiment record store not open: no run records to submit (no fabricated references)" ) );
    return;
  }
  sicnu::experiment::ExperimentStore store;
  QString err;
  if ( !store.open( src.experimentDbPath, &err ) ) {
    fail( tr( "Failed to open the experiment record store: %1" ).arg( err ) );
    return;
  }

  if ( m_session.runId.empty() ) {
    // Bind deterministically to recorded truth (same policy as the classroom
    // CLI): the newest run recorded for THIS experiment — never a guess.
    const QString expId = !src.experimentId.isEmpty()
                            ? src.experimentId
                            : QString::fromStdString( m_session.experimentId );
    if ( expId.isEmpty() ) {
      fail( tr( "Cannot determine the owning experiment: no lab project open (no cross-experiment guessing)" ) );
      return;
    }
    auto totalRes = store.listRuns( expId, QString(), QString(), 0, 1 );
    if ( !totalRes ) {
      fail( tr( "Failed to read the run record: %1" ).arg( diagText( totalRes ) ) );
      return;
    }
    const qint64 total = totalRes.value().first;
    if ( total <= 0 ) {
      fail( tr( "The experiment has no recorded runs yet (finish and save a run in the Processing Toolbox first)" ) );
      return;
    }
    auto lastRes = store.listRuns( expId, QString(), QString(), total - 1, 1 );
    if ( !lastRes || lastRes.value().second.isEmpty() ) {
      fail( tr( "Failed to read the latest run record" ) );
      return;
    }
    m_session.runId = lastRes.value().second.first().runId().toStdString();
    appendExportNote( tr( "Bound to the latest recorded run: %1" )
                        .arg( QString::fromStdString( m_session.runId ) ) );
  }

  sicnu::dataset::DatasetStore datasets;
  if ( !src.datasetDbPath.isEmpty() ) {
    QString dsErr;
    if ( !datasets.open( src.datasetDbPath, &dsErr ) )
      appendExportNote( tr( "Dataset store unavailable (%1): dataset versions will be treated as unresolved records" ).arg( dsErr ) );
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
      appendExportNote( tr( "Recorded run %1 no longer exists; the next export will rebind to the latest run" ).arg( runId ) );
    }
    fail( tr( "Capsule build failed (%1): %2" )
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
    fail( tr( "Capsule write failed: %1" ).arg( diagText( exported ) ) );
    return;
  }
  m_session.capsuleExportRef =
    ( QStringLiteral( "file:" ) + exported.value().path ).toStdString();
  appendExportNote( tr( "Capsule exported: %1 (%2 bytes)" )
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

  // Timeline doc: the lab document when it projects steps. Some shipped lab
  // documents are registry wrappers (spec_version but no steps — e.g. the
  // canonical lab12/13/14 entries); when the projection yields no steps the
  // lab-registry.json canonical source labspec is projected instead. The
  // registry is the authority on what may be opened: ids it marks
  // out-of-scope (lab8_temporal_analysis, temporal track) have no canonical
  // entry and stay fail-closed with no steps.
  auto timeline = sicnu::teaching::LabStepTimeline::fromLabDocument( labDoc, m_session.stepIndex );
  if ( timeline.steps.empty() ) {
    const Json::Value registry =
      loadJsonFile( QDir( data ).filePath( QStringLiteral( "labs/lab-registry.json" ) ) );
    const Json::Value &canonical = registry["canonical"];
    if ( canonical.isObject() && canonical.isMember( labId.toStdString() ) ) {
      const Json::Value &entry = canonical[labId.toStdString()];
      if ( entry.isObject() && entry.isMember( "source" ) && entry["source"].isString() ) {
        // Registry source paths are repo-relative ("data/labs/..."): resolve
        // under the data root exactly like grading rule refs do.
        std::string srcRef = entry["source"].asString();
        static constexpr std::string_view kDataPrefix = "data/";
        if ( srcRef.starts_with( kDataPrefix ) )
          srcRef = srcRef.substr( kDataPrefix.size() );
        const Json::Value sourceDoc = loadJsonFile( QDir( data ).filePath(
          QString::fromStdString( srcRef ) ) );
        if ( sourceDoc.isObject() ) {
          timeline = sicnu::teaching::LabStepTimeline::fromLabDocument(
            sourceDoc, m_session.stepIndex );
          // The projected steps belong to THIS course lab entry: keep the
          // wrapper id so downstream labId matching (session, feedback,
          // restore) never sees the source labspec's alias id.
          if ( timeline.ok )
            timeline.labId = labId.toStdString();
        }
      }
    }
  }
  m_workspace->setTimeline( timeline );

  // Honest readiness: passport / inspector / scientific facts are NOT
  // fabricated here. Without an injected preflight the aggregate records
  // UNKNOWN items (fail-closed) instead of a fake OK.
  Json::Value slice = findAvailabilityLabSlice( m_availability, labId.toStdString() );
  auto readiness = sicnu::teaching::LabReadiness::aggregate(
    labId.toStdString(), slice, Json::Value(), Json::Value(), Json::Value(), m_offline );
  m_workspace->setReadiness( readiness );

  // Autonomy is resolved from the real authorities with the platform's one
  // precedence rule (course < labspec): the course manifest may declare
  // sicnu.autonomy-policy/1 as autonomy_policy, and the lab document may
  // declare it as autonomy. Nothing is invented here — when no authority
  // declares a policy the projection falls back to the policy engine's own
  // fail-closed L0 and says so. Restored sessions carry a REF only; they can
  // never re-introduce a policy the authorities do not declare (no student
  // escalation path).
  std::vector<sicnu::agent::autonomy::AutonomyPolicyLayer> layers;
  std::vector<std::string> policyIssues;
  if ( m_manifest.isObject() && m_manifest.isMember( "autonomy_policy" ) ) {
    auto parsed = sicnu::agent::autonomy::parseAutonomyPolicy( m_manifest["autonomy_policy"] );
    if ( parsed.ok )
      layers.push_back( { sicnu::agent::autonomy::policy_sources::kCourse, parsed.policy } );
    else
      policyIssues.push_back( "课程策略解析失败：课程层未生效（其余层照常，结果仍 fail-closed）" );
  }
  if ( labDoc.isObject() && labDoc.isMember( "autonomy" ) ) {
    auto parsed = sicnu::agent::autonomy::parseAutonomyPolicy( labDoc["autonomy"] );
    if ( parsed.ok )
      layers.push_back( { sicnu::agent::autonomy::policy_sources::kLabspec, parsed.policy } );
    else
      policyIssues.push_back( "实验策略解析失败：实验层未生效（其余层照常，结果仍 fail-closed）" );
  }
  auto resolvedPolicy = sicnu::agent::autonomy::resolveEffectivePolicy( layers );
  auto autonomy = sicnu::teaching::AutonomyEffectiveDisplay::fromPolicyDoc(
    resolvedPolicy.toJson(), "student", "lab" );
  for ( const auto &issue : policyIssues ) autonomy.issuesZh.push_back( issue );
  if ( !resolvedPolicy.hasLevel && resolvedPolicy.mode.empty() )
    autonomy.issuesZh.push_back(
      "课程与实验均未声明自主策略：按 fail-closed L0 处理（不臆造默认策略）" );
  m_workspace->setAutonomy( autonomy );

  // The ref records which authorities were consulted, not a policy value:
  // restoring it must never be able to raise the student's autonomy.
  QString policyRef;
  if ( resolvedPolicy.hasLevel || !resolvedPolicy.mode.empty() ) {
    QStringList declared;
    for ( const auto &layer : layers ) {
      const QString tag = layer.source == std::string( sicnu::agent::autonomy::policy_sources::kLabspec )
                            ? QStringLiteral( "labspec:%1" ).arg( labId )
                            : QStringLiteral( "course:%1" ).arg(
                                m_manifest.isObject() && m_manifest.isMember( "id" )
                                  ? QString::fromStdString( m_manifest["id"].asString() )
                                  : QStringLiteral( "undergraduate_rs" ) );
      if ( !declared.contains( tag ) ) declared << tag;
    }
    policyRef = declared.join( QStringLiteral( "+" ) );
  } else {
    policyRef = QStringLiteral( "fail-closed:L0" );
  }
  m_session.autonomyPolicyRef = policyRef.toStdString();

  if ( const auto *cur = timeline.current() ) {
    QString why = tr( "(explainable workflow projection)\n" );
    why += QString::fromStdString( cur->whyHintZh );
    why += QLatin1Char( '\n' );
    why += tr( "\nProvenance badges: system fact | authoring guidance | inferred\n" );
    m_workspace->setWhyMarkdown( why );
  }
  // A stepless/fail-closed timeline keeps the projection's refusal reasons
  // in the why pane (written by the workspace) — the default banner must
  // not overwrite them.

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
