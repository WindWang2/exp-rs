// src/agent/harness/context_checkpoint.cpp
#include "context_checkpoint.h"

#include "../spatial_tools/spatial_tool.h"

#include <optional>
#include <string>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSaveFile>
#include <QStringList>
#include <QDateTime>

#include <json/reader.h>
#include <json/writer.h>

#include <algorithm>
#include <map>

namespace sicnu::agent::harness {

namespace {

QMutex gStoreMutex;

std::string compactText( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, value );
}

Json::Value parseText( const std::string &text, bool *ok )
{
  Json::Value out;
  Json::Reader reader;
  *ok = reader.parse( text, out );
  return out;
}

/// File identity for staleness checks: (size, mtime). Revision rides along
/// when the grounding recorded a governed asset.
Json::Value identityForFile( const QString &path, long long revision )
{
  Json::Value identity( Json::objectValue );
  identity["path"] = path.toStdString();
  const QFileInfo info( path );
  if ( info.exists() )
  {
    identity["size"] = static_cast<Json::Int64>( info.size() );
    identity["mtime_ms"] = static_cast<Json::Int64>( info.lastModified().toMSecsSinceEpoch() );
  }
  else
  {
    identity["size"] = static_cast<Json::Int64>( -1 );
    identity["mtime_ms"] = static_cast<Json::Int64>( -1 );
  }
  if ( revision > 0 )
    identity["revision"] = static_cast<Json::Int64>( revision );
  return identity;
}

/// Derives fact identities from the slot facts documents the compiler
/// consumed: slot -> {path, size, mtime, revision?}.
Json::Value deriveFactIdentities( const Json::Value &inputFacts )
{
  Json::Value identities( Json::objectValue );
  for ( const std::string &slot : inputFacts.getMemberNames() )
  {
    const Json::Value &doc = inputFacts[ slot ];
    Json::Value body = doc;
    if ( body.isObject() && body.isMember( "dataset_understanding" ) )
      body = body["dataset_understanding"];
    if ( !body.isObject() )
      continue;
    const std::string path = body.get( "path", "" ).asString();
    if ( path.empty() )
      continue;
    long long revision = 0;
    if ( body.isMember( "entity" ) && body["entity"].isObject() &&
         body["entity"].isMember( "revision" ) && body["entity"]["revision"].isNumeric() )
      revision = body["entity"]["revision"].asInt64();
    identities[ slot ] = identityForFile(
      QString::fromStdString( path ), revision );
  }
  return identities;
}

} // namespace

bool isKnownSessionStage( const std::string &stage )
{
  for ( const char *candidate :
        { session_stages::kIntent, session_stages::kGrounding, session_stages::kCandidates,
          session_stages::kIr, session_stages::kAnalysis, session_stages::kRepair,
          session_stages::kLower, session_stages::kExecuting, session_stages::kVerifying,
          session_stages::kDone } )
    if ( stage == candidate )
      return true;
  return false;
}

Json::Value HarnessSessionState::toJson() const
{
  Json::Value doc( Json::objectValue );
  doc["kind"] = "harness_session";
  doc["schema_version"] = schemaVersion;
  doc["session_id"] = sessionId;
  doc["saved_at"] = savedAt;
  doc["stage_cursor"] = stageCursor;
  if ( !goal.empty() )
    doc["goal"] = goal;
  if ( !intent.empty() )
    doc["intent"] = intent;
  doc["ir"] = ir;
  doc["analysis"] = analysis;
  doc["repairs"] = repairs;
  doc["refusals"] = refusals;
  doc["plan_binding"] = planBinding;
  doc["decisions"] = decisions;
  doc["failed_attempts"] = failedAttempts;
  doc["fact_identities"] = factIdentities;
  return doc;
}

std::optional<HarnessSessionState> HarnessSessionState::fromJson( const Json::Value &doc,
                                                                  std::string *error )
{
  auto fail = [ error ]( const std::string &why )
  {
    if ( error )
      *error = why;
    return std::nullopt;
  };
  if ( !doc.isObject() )
    return fail( "session document must be an object" );
  if ( doc.get( "kind", "" ).asString() != "harness_session" )
    return fail( "document kind is not harness_session" );
  if ( doc.get( "schema_version", "" ).asString() != kHarnessSessionSchemaVersion )
    return fail( "unsupported session schema_version" );
  const std::string id = doc.get( "session_id", "" ).asString();
  if ( id.empty() )
    return fail( "session_id missing" );
  HarnessSessionState state;
  state.sessionId = id;
  state.savedAt = doc.get( "saved_at", "" ).asString();
  state.stageCursor = doc.get( "stage_cursor", session_stages::kIntent ).asString();
  if ( !isKnownSessionStage( state.stageCursor ) )
    return fail( "unknown stage_cursor: " + state.stageCursor );
  state.goal = doc.get( "goal", "" ).asString();
  state.intent = doc.get( "intent", "" ).asString();
  state.ir = doc.get( "ir", Json::Value() );
  state.analysis = doc.get( "analysis", Json::Value() );
  state.repairs = doc.get( "repairs", Json::Value( Json::arrayValue ) );
  state.refusals = doc.get( "refusals", Json::Value( Json::arrayValue ) );
  state.planBinding = doc.get( "plan_binding", Json::Value() );
  state.decisions = doc.get( "decisions", Json::Value( Json::arrayValue ) );
  state.failedAttempts = doc.get( "failed_attempts", Json::Value( Json::arrayValue ) );
  state.factIdentities = doc.get( "fact_identities", Json::Value( Json::objectValue ) );
  return state;
}

int HarnessSessionState::approxTokens() const
{
  return static_cast<int>( compactText( toJson() ).size() / 4 );
}

HarnessSessionStore &HarnessSessionStore::instance()
{
  static HarnessSessionStore store;
  return store;
}

QString HarnessSessionStore::defaultDirectory() const
{
  const QString overrideDir = qEnvironmentVariable( "SICNU_HARNESS_SESSION_DIR" );
  if ( !overrideDir.isEmpty() )
    return overrideDir;
  return QDir::homePath() + QStringLiteral( "/.rs_studio/harness_sessions" );
}

QString HarnessSessionStore::directory() const
{
  QMutexLocker locker( &gStoreMutex );
  return mDirectory.isEmpty() ? defaultDirectory() : mDirectory;
}

void HarnessSessionStore::setDirectory( const QString &directory )
{
  QMutexLocker locker( &gStoreMutex );
  mDirectory = directory;
}

QString HarnessSessionStore::sessionPath( const std::string &sessionId ) const
{
  return directory() + QDir::separator() +
         QStringLiteral( "harness_session_%1.json" ).arg(
           QString::fromStdString( sessionId ) );
}

QString HarnessSessionStore::saveSession( const HarnessSessionState &state, HarnessError &error )
{
  QMutexLocker locker( &gStoreMutex );
  if ( state.sessionId.empty() || state.sessionId.size() > 128 )
  {
    error = HarnessError::make( error_codes::kInvalidParameter,
                                "session_id must be 1..128 characters" );
    return {};
  }
  for ( char c : state.sessionId )
  {
    if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
            c == '_' || c == '-' || c == '.' ) )
    {
      error = HarnessError::make( error_codes::kInvalidParameter,
                                  "session_id must stay filename-safe" );
      return {};
    }
  }

  HarnessSessionState toWrite = state;
  toWrite.savedAt = QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ).toStdString();
  std::string text = compactText( toWrite.toJson() );
  if ( static_cast<long>( text.size() ) > kMaxDocumentBytes )
  {
    // Compact and retry once; a compacted document above the bound is an
    // honest typed failure (never a silent truncation).
    Json::Value compacted = compactState( toWrite );
    // compactState returns the compacted DOCUMENT; reparse into the state.
    std::string compactError;
    std::optional<HarnessSessionState> compactStateParsed =
      HarnessSessionState::fromJson( compacted );
    if ( compactStateParsed )
    {
      toWrite = *compactStateParsed;
      toWrite.savedAt = state.savedAt;
      text = compactText( toWrite.toJson() );
    }
  }
  if ( static_cast<long>( text.size() ) > kMaxDocumentBytes )
  {
    error = HarnessError::make(
      error_codes::kInvalidParameter,
      "session document exceeds the 64 KiB bound even after compaction" );
    return {};
  }

  const QDir dir( directory() );
  if ( !dir.exists() && !dir.mkpath( "." ) )
  {
    error = HarnessError::make( error_codes::kExecutionFailed,
                                "cannot create session directory: " +
                                  directory().toStdString() );
    return {};
  }

  // Evict oldest sessions beyond the bound (by file mtime).
  QFileInfoList sessions =
    dir.entryInfoList( QStringList() << "harness_session_*.json", QDir::Files, QDir::Time );
  while ( sessions.size() >= kMaxSessions )
  {
    QFile::remove( dir.filePath( sessions.takeLast().absoluteFilePath() ) );
  }

  const QString finalPath = sessionPath( state.sessionId );
  QSaveFile file( finalPath );
  if ( !file.open( QIODevice::WriteOnly ) ||
       file.write( QByteArray::fromStdString( text ) ) < 0 || !file.commit() )
  {
    error = HarnessError::make( error_codes::kExecutionFailed,
                                "failed to write session checkpoint: " +
                                  file.errorString().toStdString() );
    return {};
  }
  return finalPath;
}

std::optional<HarnessSessionState> HarnessSessionStore::loadSession( const std::string &sessionId,
                                                                     HarnessError &error ) const
{
  QMutexLocker locker( &gStoreMutex );
  const QString path = sessionPath( sessionId );
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    error = HarnessError::make( error_codes::kWorkflowNotFound,
                                "no such harness session: " + sessionId );
    return std::nullopt;
  }
  const QByteArray bytes = file.readAll();
  bool ok = false;
  const Json::Value doc = parseText( std::string( bytes.constData(), bytes.size() ), &ok );
  std::string parseError;
  if ( !ok )
  {
    error = HarnessError::make( error_codes::kInvalidPlan,
                                "corrupt session document (unreadable JSON)" );
    return std::nullopt;
  }
  auto state = HarnessSessionState::fromJson( doc, &parseError );
  if ( !state )
  {
    error = HarnessError::make( error_codes::kInvalidPlan, parseError );
    return std::nullopt;
  }
  return state;
}

Json::Value HarnessSessionStore::listSessions() const
{
  QMutexLocker locker( &gStoreMutex );
  Json::Value list( Json::arrayValue );
  const QDir dir( directory() );
  const QFileInfoList files = dir.entryInfoList( QStringList() << "harness_session_*.json",
                                                 QDir::Files, QDir::Time );
  for ( const QFileInfo &info : files )
  {
    QFile file( info.absoluteFilePath() );
    if ( !file.open( QIODevice::ReadOnly ) )
      continue;
    const QByteArray bytes = file.readAll();
    bool ok = false;
    const Json::Value doc = parseText( std::string( bytes.constData(), bytes.size() ), &ok );
    if ( !ok )
      continue;
    Json::Value entry( Json::objectValue );
    entry["session_id"] = doc.get( "session_id", "" ).asString();
    entry["saved_at"] = doc.get( "saved_at", "" ).asString();
    entry["stage_cursor"] = doc.get( "stage_cursor", "" ).asString();
    entry["goal"] = doc.get( "goal", "" ).asString();
    entry["intent"] = doc.get( "intent", "" ).asString();
    entry["bytes"] = static_cast<Json::Int64>( bytes.size() );
    entry["approx_tokens"] = static_cast<Json::Int>( bytes.size() / 4 );
    list.append( entry );
  }
  return list;
}

bool HarnessSessionStore::deleteSession( const std::string &sessionId )
{
  QMutexLocker locker( &gStoreMutex );
  return QFile::exists( sessionPath( sessionId ) ) &&
         QFile::remove( sessionPath( sessionId ) );
}

Json::Value HarnessSessionStore::stalenessReport( const HarnessSessionState &state ) const
{
  Json::Value report( Json::objectValue );
  for ( const std::string &slot : state.factIdentities.getMemberNames() )
  {
    const Json::Value &saved = state.factIdentities[ slot ];
    Json::Value entry( Json::objectValue );
    entry["saved"] = saved;
    if ( !saved.isObject() || saved.get( "path", "" ).asString().empty() )
    {
      entry["stale"] = true;
      entry["reason"] = "no_identity";
      report[ slot ] = entry;
      continue;
    }
    const QString path = QString::fromStdString( saved.get( "path", "" ).asString() );
    const long long revision = saved.get( "revision", 0 ).asInt64();
    const Json::Value current = identityForFile( path, revision );
    entry["current"] = current;
    const bool sameSize = saved.get( "size", 0 ).asInt64() == current.get( "size", 0 ).asInt64();
    const bool sameMtime =
      saved.get( "mtime_ms", 0 ).asInt64() == current.get( "mtime_ms", 0 ).asInt64();
    if ( current.get( "size", 0 ).asInt64() < 0 )
    {
      entry["stale"] = true;
      entry["reason"] = "file_missing";
    }
    else if ( !sameSize || !sameMtime )
    {
      entry["stale"] = true;
      entry["reason"] = "file_changed";
    }
    else
    {
      entry["stale"] = false;
    }
    report[ slot ] = entry;
  }
  return report;
}

std::optional<HarnessSessionStore::ResumeResult> HarnessSessionStore::resumeSession(
  const std::string &sessionId, HarnessError &error ) const
{
  auto state = loadSession( sessionId, error );
  if ( !state )
    return std::nullopt;
  ResumeResult result;
  result.state = *state;
  result.staleSlots = stalenessReport( *state );
  bool anyStale = false;
  for ( const std::string &slot : result.staleSlots.getMemberNames() )
    if ( result.staleSlots[ slot ].get( "stale", false ).asBool() )
      anyStale = true;
  result.rewindToStage =
    anyStale ? std::string( session_stages::kGrounding ) : result.state.stageCursor;
  return result;
}

Json::Value HarnessSessionStore::compactState( const HarnessSessionState &state )
{
  HarnessSessionState compacted = state;
  // The IR is the scientific core — keep it whole. The analysis keeps only
  // the verdict and the issue list (checks details and fact bodies drop).
  if ( state.analysis.isObject() )
  {
    Json::Value slim( Json::objectValue );
    slim["verdict"] = state.analysis.get( "verdict", "" ).asString();
    slim["issues"] = state.analysis.get( "issues", Json::Value( Json::arrayValue ) );
    compacted.analysis = slim;
  }
  compacted.factIdentities = state.factIdentities; // identities are tiny and load-bearing
  Json::Value doc = compacted.toJson();
  // Drop null/empty optional members to keep the compact form minimal.
  if ( doc["ir"].isNull() )
    doc.removeMember( "ir" );
  if ( doc["analysis"].isNull() )
    doc.removeMember( "analysis" );
  if ( doc["plan_binding"].isNull() )
    doc.removeMember( "plan_binding" );
  return doc;
}

using namespace sicnu::agent::spatial_tools;

namespace {

Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

class WorkflowSessionTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:workflow_session"; }
    std::string displayName() const override { return "Workflow Session Checkpoint"; }
    std::string description() const override
    {
      return "Long-task compiler context: checkpoint (save), resume with "
             "stale-fact invalidation, list, delete, and staleness report. "
             "A resumed session keeps decisions and failed-attempt history; "
             "facts whose files changed are marked stale and rewind the "
             "pipeline to grounding.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "session", "checkpoint", "resume", "context" };
    }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value action( Json::objectValue );
      action["type"] = "string";
      action["enum"] = Json::Value( Json::arrayValue );
      for ( const char *a : { "save", "resume", "list", "delete", "staleness" } )
        action["enum"].append( a );
      action["description"] = "Checkpoint action.";
      props["action"] = action;
      Json::Value sessionId( Json::objectValue );
      sessionId["type"] = "string";
      sessionId["description"] = "Session id (filename-safe).";
      props["session_id"] = sessionId;
      Json::Value ir( Json::objectValue );
      ir["type"] = "object";
      ir["description"] = "(save) normalized WorkflowIR document.";
      props["ir"] = ir;
      Json::Value analysis( Json::objectValue );
      analysis["type"] = "object";
      analysis["description"] = "(save) last analysis document.";
      props["analysis"] = analysis;
      Json::Value planBinding( Json::objectValue );
      planBinding["type"] = "object";
      planBinding["description"] = "(save) {run_id, plan_id, plan_fingerprint, ...}.";
      props["plan_binding"] = planBinding;
      Json::Value stage( Json::objectValue );
      stage["type"] = "string";
      stage["description"] = "(save) stage cursor: intent|grounding|candidates|ir|analysis|"
                             "repair|lower|executing|verifying|done.";
      props["stage_cursor"] = stage;
      Json::Value facts( Json::objectValue );
      facts["type"] = "object";
      facts["description"] = "(save) slot facts documents — file identities are derived "
                             "for staleness checking.";
      props["input_facts"] = facts;
      Json::Value required( Json::arrayValue );
      required.append( "action" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["ok"] = Json::Value( Json::booleanValue );
      props["sessions"] = Json::Value( Json::arrayValue );
      props["stale_slots"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isObject() || !input.isMember( "action" ) || !input["action"].isString() )
        return SpatialToolResult::failure( "missing string parameter 'action'",
                                           error_codes::kInvalidParameter, "validation" );
      const std::string action = input["action"].asString();
      HarnessSessionStore &store = HarnessSessionStore::instance();

      if ( action == "list" )
      {
        Json::Value out( Json::objectValue );
        out["ok"] = true;
        out["sessions"] = store.listSessions();
        out["store_directory"] = store.directory().toStdString();
        return SpatialToolResult::ok( std::move( out ) );
      }

      const std::string sessionId = input.get( "session_id", "" ).asString();
      if ( sessionId.empty() )
        return SpatialToolResult::failure( "missing string parameter 'session_id'",
                                           error_codes::kInvalidParameter, "validation" );

      if ( action == "delete" )
      {
        Json::Value out( Json::objectValue );
        out["ok"] = store.deleteSession( sessionId );
        return SpatialToolResult::ok( std::move( out ) );
      }

      if ( action == "staleness" )
      {
        HarnessError error;
        auto state = store.loadSession( sessionId, error );
        if ( !state )
          return SpatialToolResult::failure( error.summary, error.code, "validation" );
        Json::Value out( Json::objectValue );
        out["ok"] = true;
        out["stale_slots"] = store.stalenessReport( *state );
        return SpatialToolResult::ok( std::move( out ) );
      }

      if ( action == "resume" )
      {
        HarnessError error;
        auto resumed = store.resumeSession( sessionId, error );
        if ( !resumed )
          return SpatialToolResult::failure( error.summary, error.code, "validation" );
        Json::Value out( Json::objectValue );
        out["ok"] = true;
        out["session"] = resumed->state.toJson();
        out["stale_slots"] = resumed->staleSlots;
        out["rewind_to_stage"] = resumed->rewindToStage;
        out["note"] = resumed->rewindToStage == session_stages::kGrounding
                        ? "Some fact files changed since the checkpoint — re-ground stale "
                          "slots before continuing; decisions and attempts survived"
                        : "Facts still current; continue from the saved stage";
        return SpatialToolResult::ok( std::move( out ) );
      }

      if ( action == "save" )
      {
        HarnessSessionState state;
        state.sessionId = sessionId;
        state.stageCursor = input.get( "stage_cursor", session_stages::kIntent ).asString();
        if ( !isKnownSessionStage( state.stageCursor ) )
          return SpatialToolResult::failure(
            "unknown stage_cursor: " + state.stageCursor,
            error_codes::kInvalidParameter, "validation" );
        state.goal = input.get( "goal", "" ).asString();
        state.intent = input.get( "intent", "" ).asString();
        state.ir = input.get( "ir", Json::Value() );
        state.analysis = input.get( "analysis", Json::Value() );
        state.repairs = input.get( "repairs", Json::Value( Json::arrayValue ) );
        state.refusals = input.get( "refusals", Json::Value( Json::arrayValue ) );
        state.planBinding = input.get( "plan_binding", Json::Value() );
        state.decisions = input.get( "decisions", Json::Value( Json::arrayValue ) );
        state.failedAttempts = input.get( "failed_attempts", Json::Value( Json::arrayValue ) );
        state.factIdentities =
          deriveFactIdentities( input.get( "input_facts", Json::Value( Json::objectValue ) ) );
        HarnessError error;
        const QString path = store.saveSession( state, error );
        if ( path.isEmpty() )
          return SpatialToolResult::failure( error.summary, error.code, "runtime" );
        Json::Value out( Json::objectValue );
        out["ok"] = true;
        out["path"] = path.toStdString();
        out["stage_cursor"] = state.stageCursor;
        out["approx_tokens"] = state.approxTokens();
        out["next"] = "harness:workflow_session {action: resume, session_id} — survive "
                      "process restarts and re-ground stale facts";
        return SpatialToolResult::ok( std::move( out ) );
      }

      return SpatialToolResult::failure( "unknown action: " + action,
                                         error_codes::kInvalidParameter, "validation" );
    }
};

} // namespace

void registerContextSessionTools()
{
  SpatialToolRegistry::instance().registerTool( std::make_shared<WorkflowSessionTool>() );
}

} // namespace sicnu::agent::harness
