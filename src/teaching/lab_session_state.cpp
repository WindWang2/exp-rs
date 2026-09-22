#include "teaching/lab_session_state.h"

#include <fstream>
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
  "run_id", "step_index", "evidence_refs", "autonomy_policy_ref",
  "last_validation_summary", "capsule_export_ref", "mode", "lab_status",
};

bool isAllowedKey( const std::string &k )
{
  for ( const char *a : kAllowedKeys )
    if ( k == a ) return true;
  return false;
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
  root["autonomy_policy_ref"] = autonomyPolicyRef;
  root["last_validation_summary"] = lastValidationSummary.isNull()
                                      ? Json::Value( Json::objectValue )
                                      : lastValidationSummary;
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
  std::string errs;
  std::istringstream stream( bytes );
  if ( !Json::parseFromStream( b, stream, &root, &errs ) ) {
    LabSessionState s;
    s.issuesZh.push_back( "JSON 解析失败: " + errs );
    return s;
  }
  return fromJson( root );
}

bool LabSessionState::saveToFile( const std::string &path ) const
{
  if ( !ok ) return false;
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  if ( !out ) return false;
  out << serialize();
  return static_cast<bool>( out );
}

LabSessionState LabSessionState::loadFromFile( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in ) {
    LabSessionState s;
    s.issuesZh.push_back( "无法读取会话文件" );
    return s;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return deserialize( ss.str() );
}

} // namespace sicnu::teaching
