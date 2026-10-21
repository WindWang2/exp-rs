#include "teaching/lab_session_state.h"

#include "platform/durable_sidecar.h"
#include "teaching/verdict_stamp.h"

#include <exception>
#include <sstream>

namespace sicnu::teaching {
namespace {

std::string strOf( const Json::Value &v, const char *key )
{
  if ( !v.isObject() || !v.isMember( key ) || !v[key].isString() ) return {};
  return v[key].asString();
}

bool requireObject( const Json::Value &doc, std::vector<std::string> &issues )
{
  if ( doc.isObject() ) return true;
  issues.push_back( "session 非 JSON 对象" );
  return false;
}

const char *kAllowedKeys[] = {
  "schema", "session_id", "course_id", "module_id", "lab_id", "experiment_id",
  "run_id", "step_index", "evidence_refs", "artifact_path",
  "autonomy_policy_ref",
  "last_validation_summary", "verdict_artifact_stamp", "capsule_export_ref",
  "mode", "lab_status",
};

bool isAllowedKey( const std::string &k )
{
  for ( const char *a : kAllowedKeys )
    if ( k == a ) return true;
  return false;
}

/// The status the recorded summary alone justifies. An absent/empty summary
/// justifies no verdict state at all (InProgress), matching the writer:
/// the cockpit derives lab_status from the projection at validation time.
LabUiStatus derivedLabStatus( const Json::Value &summary )
{
  if ( !summary.isObject() || summary.empty() )
    return LabUiStatus::InProgress;
  const std::string overall = strOf( summary, "overall_status" );
  if ( overall == "pass" ) return LabUiStatus::Completed;
  if ( overall == "fail" ) return LabUiStatus::NeedsCorrection;
  return LabUiStatus::PendingVerify;
}

} // namespace

LabSessionState LabSessionState::makeNew( const std::string &sessionId,
                                          const std::string &courseId,
                                          const std::string &labId,
                                          ExperienceMode mode )
{
  LabSessionState s;
  if ( sessionId.empty() || courseId.empty() || labId.empty() ) {
    s.issuesZh.push_back( "session/course/lab id 不能为空" );
    return s;
  }
  s.sessionId = sessionId;
  s.courseId = courseId;
  s.labId = labId;
  s.mode = mode;
  s.labStatus = LabUiStatus::InProgress;
  s.ok = true;
  return s;
}

LabSessionState LabSessionState::fromJson( const Json::Value &doc )
{
  LabSessionState s;
  if ( !requireObject( doc, s.issuesZh ) ) return s;

  // Reject unknown keys (fail-closed).
  const auto names = doc.getMemberNames();
  for ( const auto &n : names ) {
    if ( !isAllowedKey( n ) ) {
      s.issuesZh.push_back( "未知字段: " + n );
      return s;
    }
  }

  const std::string schema = strOf( doc, "schema" );
  if ( schema != kTeachingSessionSchema ) {
    s.issuesZh.push_back( std::string( "schema 不匹配: " )
                          + ( schema.empty() ? "<empty>" : schema ) );
    return s;
  }
  s.schema = schema;
  s.sessionId = strOf( doc, "session_id" );
  s.courseId = strOf( doc, "course_id" );
  s.moduleId = strOf( doc, "module_id" );
  s.labId = strOf( doc, "lab_id" );
  s.experimentId = strOf( doc, "experiment_id" );
  s.runId = strOf( doc, "run_id" );
  if ( doc.isMember( "artifact_path" ) ) {
    if ( !doc["artifact_path"].isString() ) {
      s.issuesZh.push_back( "artifact_path 类型错误" );
      return s;
    }
    s.artifactPath = doc["artifact_path"].asString();
  }
  s.autonomyPolicyRef = strOf( doc, "autonomy_policy_ref" );
  s.capsuleExportRef = strOf( doc, "capsule_export_ref" );

  if ( s.sessionId.empty() || s.courseId.empty() || s.labId.empty() ) {
    s.issuesZh.push_back( "缺少 session_id/course_id/lab_id" );
    return s;
  }

  if ( doc.isMember( "step_index" ) ) {
    if ( !doc["step_index"].isInt() && !doc["step_index"].isUInt() ) {
      s.issuesZh.push_back( "step_index 类型错误" );
      return s;
    }
    s.stepIndex = doc["step_index"].asInt();
    if ( s.stepIndex < 0 ) {
      s.issuesZh.push_back( "step_index 为负" );
      return s;
    }
  }

  if ( doc.isMember( "evidence_refs" ) ) {
    if ( !doc["evidence_refs"].isArray() ) {
      s.issuesZh.push_back( "evidence_refs 非数组" );
      return s;
    }
    for ( const auto &e : doc["evidence_refs"] ) {
      if ( !e.isString() ) {
        s.issuesZh.push_back( "evidence_refs 含非字符串" );
        return s;
      }
      s.evidenceRefs.push_back( e.asString() );
    }
  }

  if ( doc.isMember( "last_validation_summary" ) ) {
    if ( !doc["last_validation_summary"].isObject()
         && !doc["last_validation_summary"].isNull() ) {
      s.issuesZh.push_back( "last_validation_summary 类型错误" );
      return s;
    }
    s.lastValidationSummary = doc["last_validation_summary"];
  }

  if ( doc.isMember( "verdict_artifact_stamp" ) ) {
    if ( !doc["verdict_artifact_stamp"].isString() ) {
      s.issuesZh.push_back( "verdict_artifact_stamp 类型错误" );
      return s;
    }
    s.verdictArtifactStamp = doc["verdict_artifact_stamp"].asString();
  }

  if ( doc.isMember( "mode" ) ) {
    if ( !experienceModeFromWire( strOf( doc, "mode" ), s.mode ) ) {
      s.issuesZh.push_back( "未知 mode" );
      return s;
    }
  }

  if ( doc.isMember( "lab_status" ) ) {
    if ( !labUiStatusFromWire( strOf( doc, "lab_status" ), s.labStatus ) ) {
      s.issuesZh.push_back( "未知 lab_status" );
      return s;
    }
  }

  // Verdict-state authority (R18-E T101): lab_status is a projection of the
  // recorded summary, never an independent truth. A persisted verdict-state
  // that contradicts the summary on disk (hand-edited "completed" over an
  // empty or failed summary) is corrected to the summary-derived status.
  if ( s.labStatus == LabUiStatus::Completed || s.labStatus == LabUiStatus::NeedsCorrection
       || s.labStatus == LabUiStatus::PendingVerify ) {
    const LabUiStatus derived = derivedLabStatus( s.lastValidationSummary );
    if ( derived != s.labStatus ) {
      s.issuesZh.push_back( "lab_status 与记录的验证摘要不一致（已按摘要纠正）" );
      s.labStatus = derived;
    }
  }

  s.ok = true;
  return s;
}

Json::Value LabSessionState::toJson() const
{
  Json::Value root( Json::objectValue );
  root["schema"] = schema;
  root["session_id"] = sessionId;
  root["course_id"] = courseId;
  root["module_id"] = moduleId;
  root["lab_id"] = labId;
  root["experiment_id"] = experimentId;
  root["run_id"] = runId;
  root["step_index"] = stepIndex;
  Json::Value ev( Json::arrayValue );
  for ( const auto &e : evidenceRefs ) ev.append( e );
  root["evidence_refs"] = ev;
  root["artifact_path"] = artifactPath;
  root["autonomy_policy_ref"] = autonomyPolicyRef;
  root["last_validation_summary"] = lastValidationSummary.isNull()
                                      ? Json::Value( Json::objectValue )
                                      : lastValidationSummary;
  root["verdict_artifact_stamp"] = verdictArtifactStamp;
  root["capsule_export_ref"] = capsuleExportRef;
  root["mode"] = experienceModeWire( mode );
  root["lab_status"] = labUiStatusWire( labStatus );
  return root;
}

std::string LabSessionState::serialize() const
{
  Json::StreamWriterBuilder b;
  b["indentation"] = "  ";
  b["emitUTF8"] = true;
  return Json::writeString( b, toJson() );
}

LabSessionState LabSessionState::deserialize( const std::string &bytes )
{
  Json::Value root;
  Json::CharReaderBuilder b;
  b["collectComments"] = false;
  // Pinned stackLimit + typed catch (grader_json.cpp discipline): jsoncpp
  // THROWS out of the reader on a stackLimit violation, and a session file
  // is hand-editable input in this stack's threat model (lab/session_state.cpp
  // defends tampering on these documents) — a nesting bomb must fail closed
  // as a parse issue, never escape as an unhandled Json::RuntimeError.
  b["stackLimit"] = 256;
  std::string errs;
  std::istringstream stream( bytes );
  try
  {
    if ( !Json::parseFromStream( b, stream, &root, &errs ) ) {
      LabSessionState s;
      s.issuesZh.push_back( "JSON 解析失败: " + errs );
      return s;
    }
  }
  catch ( const std::exception &e )
  {
    LabSessionState s;
    s.issuesZh.push_back( "JSON 解析失败: " + std::string( e.what() ) );
    return s;
  }
  return fromJson( root );
}

bool LabSessionState::saveToFile( const std::string &path ) const
{
  if ( !ok ) return false;
  // R6 single sidecar write authority (#1466): staged temp + fsync + atomic
  // publish, same authority the batch side already routes through
  // (batch_assessment.cpp). The old in-place ofstream truncation tore the
  // student's progress file on a crash mid-write, and the fail-closed
  // reload then lost the whole session.
  //
  // Verdict binding (R18-E T102): when a verdict is on record for an
  // artifact, stamp the artifact bytes WITH the summary digest. Taken here
  // (the single write authority) rather than by callers: the cockpit saves
  // immediately after validation, so save-time bytes are the graded bytes.
  // A NEW verdict (different summary digest) re-stamps; a later artifact
  // edit is caught by loadFromFile's re-derivation instead of being
  // silently re-blessed.
  LabSessionState out = *this;
  if ( out.lastValidationSummary.isObject() && !out.lastValidationSummary.empty()
       && !out.artifactPath.empty() ) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    writer["commentStyle"] = "None";
    const std::string summaryDigest = verdictSummaryDigest(
      Json::writeString( writer, out.lastValidationSummary ) );
    const VerdictStamp existing = parseVerdictStamp( out.verdictArtifactStamp );
    const bool restamp = summaryDigest.empty() || !existing.parsed
                         || existing.summaryDigest != summaryDigest;
    if ( restamp && !summaryDigest.empty() ) {
      const std::string stamp = makeVerdictStamp( summaryDigest, out.artifactPath );
      // An unreadable artifact keeps the previous binding (or none): no
      // binding is better than one over the wrong bytes.
      if ( !stamp.empty() )
        out.verdictArtifactStamp = stamp;
    }
  }
  const sicnu::platform::sidecar::WriteResult result =
    sicnu::platform::sidecar::write( { path, out.serialize(), "" } );
  return static_cast<bool>( result );
}

LabSessionState LabSessionState::loadFromFile( const std::string &path )
{
  // Sidecar read resolution (same authority as the write; oversize counts
  // as unreadable, bounding the read that used to be an unbounded rdbuf).
  const sicnu::platform::sidecar::ReadResult result =
    sicnu::platform::sidecar::read( path, "" );
  if ( result.source == sicnu::platform::sidecar::ReadSource::Missing ) {
    LabSessionState s;
    s.issuesZh.push_back( "无法读取会话文件" );
    return s;
  }
  LabSessionState s = deserialize( result.bytes );
  if ( !s.ok )
    return s;
  // Verdict binding re-derivation (R18-E T102). A stamp on record means the
  // verdict claims specific artifact bytes and a specific summary text;
  // a mismatch on either (re-submission, or an edited summary) downgrades
  // the recorded verdict to indeterminate instead of displaying it. A
  // vanished artifact cannot confirm a pass either — but a LEGACY session
  // (no stamp at all) keeps its recorded verdict: failing closed here
  // would erase every pre-stamp student session on upgrade.
  const VerdictStamp stamp = parseVerdictStamp( s.verdictArtifactStamp );
  if ( stamp.parsed && s.lastValidationSummary.isObject()
       && !s.lastValidationSummary.empty() ) {
    std::string issue;
    bool stale = false;
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    writer["commentStyle"] = "None";
    const std::string summaryDigest = verdictSummaryDigest(
      Json::writeString( writer, s.lastValidationSummary ) );
    if ( summaryDigest.empty() || summaryDigest != stamp.summaryDigest ) {
      stale = true;
      issue = "验证摘要在盖章后被修改（按不确定处理）";
    } else {
      const std::string contentDigest =
        verdictArtifactContentDigest( s.artifactPath );
      if ( contentDigest.empty() ) {
        stale = true;
        issue = "被验证的工件缺失或不可读，无法复核记录的判定（按不确定处理）";
      } else if ( contentDigest != stamp.contentDigest ) {
        stale = true;
        issue = "工件与记录判定时不一致（按不确定处理，请重新验证）";
      }
    }
    if ( stale ) {
      Json::Value summary = s.lastValidationSummary;
      summary["overall_status"] = "indeterminate";
      summary["overall_counts_as_pass"] = false;
      Json::Value issues( Json::arrayValue );
      if ( summary.isMember( "issues_zh" ) && summary["issues_zh"].isArray() )
        issues = summary["issues_zh"];
      issues.append( issue );
      summary["issues_zh"] = issues;
      s.lastValidationSummary = std::move( summary );
      s.verdictArtifactStamp.clear();
      s.issuesZh.push_back( issue );
      if ( s.labStatus == LabUiStatus::Completed
           || s.labStatus == LabUiStatus::NeedsCorrection )
        s.labStatus = LabUiStatus::PendingVerify;
    }
  }
  return s;
}

} // namespace sicnu::teaching
