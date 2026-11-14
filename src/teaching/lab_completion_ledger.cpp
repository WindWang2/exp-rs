#include "teaching/lab_completion_ledger.h"

#include "platform/durable_sidecar.h"
#include "teaching/verdict_stamp.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace sicnu::teaching
{
namespace
{

std::string strOf( const Json::Value &v, const char *key )
{
  if ( !v.isObject() || !v.isMember( key ) || !v[key].isString() ) return {};
  return v[key].asString();
}

/// Course namespace for the duplicate-verdict guard. Two courses may use
/// the same lab id AND the same verdict document (a shared course pack);
/// refusing to arbitrate there would wipe BOTH courses' real evidence.
/// Within one course namespace the copy-paste forgery rule stays absolute.
std::string digestNamespace( const LabCompletionEntry &entry )
{
  return entry.courseId + "/" + entry.verdictDigest;
}

/// Two courses may legitimately use the SAME lab id. The plain labId key
/// is kept as the primary (all pre-R30 consumers read it); a SECOND entry
/// for that lab is namespaced so neither course's evidence is overwritten.
const std::string kCourseEntryPrefix = "course::";
/// A namespaced key still needs a reversible spelling for a LEGACY entry
/// whose courseId is empty (pre-R30 documents have no course identity).
const std::string kLegacyCourseToken = "__legacy__";

std::string entryKey( const std::string &labId, const std::string &courseId )
{
  return kCourseEntryPrefix
         + ( courseId.empty() ? kLegacyCourseToken : courseId ) + "::" + labId;
}

bool parseEntryKey( const std::string &key, std::string *labIdOut,
                    std::string *courseIdOut )
{
  if ( key.rfind( kCourseEntryPrefix, 0 ) != 0 )
  {
    *labIdOut = key;
    courseIdOut->clear();
    return true;
  }
  const std::size_t separator = key.find( "::", kCourseEntryPrefix.size() );
  if ( separator == std::string::npos || separator + 2 >= key.size() )
    return false;
  const std::string course = key.substr( kCourseEntryPrefix.size(),
                                         separator - kCourseEntryPrefix.size() );
  *courseIdOut = course == kLegacyCourseToken ? std::string() : course;
  *labIdOut = key.substr( separator + 2 );
  return !labIdOut->empty() && !course.empty();
}

} // namespace

bool LabCompletionLedger::recordVerdict( const LabSessionState &session,
                                         const std::string &sessionRef )
{
  const VerdictStamp stamp = parseVerdictStamp( session.verdictArtifactStamp );
  if ( !session.ok || session.labId.empty() ) {
    issuesZh.push_back( "会话无效，不能记录完成" );
    return false;
  }
  if ( !stamp.parsed ) {
    issuesZh.push_back( "会话判定缺少工件绑定（未盖章的判定不是完成证据）" );
    return false;
  }
  if ( !session.lastValidationSummary.isObject()
       || strOf( session.lastValidationSummary, "overall_status" ) != "pass" ) {
    return false; // not a pass — nothing to record, not an error
  }
  for ( auto &entry : completed ) {
    // R30-T07: identity, not just the lab label. A session of lab A must
    // never update a B entry, and two courses sharing a lab id must not
    // overwrite each other's completion evidence.
    if ( entry.labId != session.labId )
      continue;
    // R30-T07 R1 (P1-7): a LEGACY entry has no stored courseId; before
    // letting a new course's session update it, derive its true course from
    // the referenced (verified) session. Otherwise course-2 would overwrite
    // course-1's completion evidence for a shared lab id.
    std::string effectiveCourse = entry.courseId;
    if ( effectiveCourse.empty() && !entry.sessionRef.empty() ) {
      const LabSessionState legacy =
        LabSessionState::loadFromFile( entry.sessionRef );
      if ( legacy.ok )
        effectiveCourse = legacy.courseId;
    }
    if ( !effectiveCourse.empty() && effectiveCourse != session.courseId )
      continue;
    // Latest verified verdict wins; the entry keeps its evidence chain.
    entry.sessionRef = sessionRef;
    entry.verdictDigest = stamp.summaryDigest;
    entry.artifactStamp = session.verdictArtifactStamp;
    entry.courseId = session.courseId;
    entry.sessionId = session.sessionId;
    return true;
  }
  LabCompletionEntry entry;
  entry.labId = session.labId;
  entry.sessionRef = sessionRef;
  entry.verdictDigest = stamp.summaryDigest;
  entry.artifactStamp = session.verdictArtifactStamp;
  entry.courseId = session.courseId;
  entry.sessionId = session.sessionId;
  completed.push_back( entry );
  // Deterministic order: sorted by (lab id, course id).
  std::sort( completed.begin(), completed.end(),
             []( const LabCompletionEntry &a, const LabCompletionEntry &b ) {
               if ( a.labId != b.labId ) return a.labId < b.labId;
               return a.courseId < b.courseId;
             } );
  return true;
}

Json::Value LabCompletionLedger::toJson() const
{
  Json::Value root( Json::objectValue );
  root["schema"] = schema;
  Json::Value done( Json::objectValue );
  std::set<std::string> usedLabIds;
  for ( const auto &entry : completed ) {
    Json::Value e( Json::objectValue );
    e["session_ref"] = entry.sessionRef;
    e["verdict_digest"] = entry.verdictDigest;
    e["artifact_stamp"] = entry.artifactStamp;
    // Additive optional identity. Legacy documents stay byte-compatible when
    // the fields are empty (pre-R30 entries / fresh states).
    if ( !entry.courseId.empty() ) e["course_id"] = entry.courseId;
    if ( !entry.sessionId.empty() ) e["session_id"] = entry.sessionId;
    const bool firstForLab = usedLabIds.insert( entry.labId ).second;
    const std::string key = firstForLab
                              ? entry.labId
                              : entryKey( entry.labId, entry.courseId );
    done[key] = e;
  }
  root["completed"] = done;
  return root;
}

LabCompletionLedger LabCompletionLedger::fromJson( const Json::Value &doc )
{
  LabCompletionLedger ledger;
  if ( !doc.isObject() ) {
    ledger.issuesZh.push_back( "完成账本非 JSON 对象" );
    return ledger;
  }
  const auto names = doc.getMemberNames();
  for ( const auto &n : names ) {
    if ( n != "schema" && n != "completed" ) {
      ledger.issuesZh.push_back( "未知字段: " + n );
      return ledger;
    }
  }
  if ( strOf( doc, "schema" ) != kLabCompletionLedgerSchema ) {
    ledger.issuesZh.push_back( "schema 不匹配" );
    return ledger;
  }
  if ( !doc.isMember( "completed" ) || !doc["completed"].isObject() ) {
    ledger.issuesZh.push_back( "completed 必须是对象" );
    return ledger;
  }
  std::set<std::string> seenDigests;
  const auto &completed = doc["completed"];
  const auto keys = completed.getMemberNames();
  for ( const auto &key : keys ) {
    std::string labId;
    std::string courseFromKey;
    if ( !parseEntryKey( key, &labId, &courseFromKey ) || labId.empty() ) {
      ledger.issuesZh.push_back( "completed 含空或非法 lab id" );
      return ledger;
    }
    const Json::Value &e = completed[key];
    if ( !e.isObject() ) {
      ledger.issuesZh.push_back( "完成条目必须是对象: " + key );
      return ledger;
    }
    const auto entryKeys = e.getMemberNames();
    for ( const auto &k : entryKeys ) {
      if ( k != "session_ref" && k != "verdict_digest" && k != "artifact_stamp"
           && k != "course_id" && k != "session_id" ) {
        ledger.issuesZh.push_back( "完成条目未知字段: " + key + "." + k );
        return ledger;
      }
      // Wrong-TYPE optional identity is a typed refusal, never a silent
      // empty fallback that would drop the course scope.
      if ( ( k == "course_id" || k == "session_id" ) && !e[k].isString() ) {
        ledger.issuesZh.push_back( "完成条目身份字段类型错误: " + key + "." + k );
        return ledger;
      }
    }
    LabCompletionEntry entry;
    entry.labId = labId;
    entry.sessionRef = strOf( e, "session_ref" );
    entry.verdictDigest = strOf( e, "verdict_digest" );
    entry.artifactStamp = strOf( e, "artifact_stamp" );
    entry.courseId = strOf( e, "course_id" );
    entry.sessionId = strOf( e, "session_id" );
    // A namespaced key and its course_id field must agree; a hand-edited
    // document must not be able to move an entry into another course.
    if ( !courseFromKey.empty() )
    {
      if ( !entry.courseId.empty() && entry.courseId != courseFromKey )
      {
        ledger.issuesZh.push_back( "完成条目课程身份与键不一致: " + key );
        return ledger;
      }
      entry.courseId = courseFromKey;
    }
    if ( entry.sessionRef.empty() || entry.verdictDigest.size() != 64 ) {
      ledger.issuesZh.push_back( "完成条目缺证据链: " + labId );
      return ledger;
    }
    if ( !seenDigests.insert( digestNamespace( entry ) ).second ) {
      // Two labs in the SAME course claiming one verdict document — a
      // copy-paste forgery; refuse to arbitrate, keep neither.
      ledger.issuesZh.push_back( "多个条目引用同一判定文档（拒绝仲裁）" );
      ledger.completed.clear();
      return ledger;
    }
    ledger.completed.push_back( entry );
  }
  std::sort( ledger.completed.begin(), ledger.completed.end(),
             []( const LabCompletionEntry &a, const LabCompletionEntry &b ) {
               if ( a.labId != b.labId ) return a.labId < b.labId;
               return a.courseId < b.courseId;
             } );
  ledger.ok = true;
  return ledger;
}

LabCompletionLedger LabCompletionLedger::loadVerified( const std::string &ledgerPath,
                                                        const std::string &courseId )
{
  const sicnu::platform::sidecar::ReadResult result =
    sicnu::platform::sidecar::read( ledgerPath, "" );
  LabCompletionLedger ledger;
  if ( result.source == sicnu::platform::sidecar::ReadSource::Missing ) {
    ledger.issuesZh.push_back( "无法读取完成账本" );
    return ledger;
  }
  Json::Value doc;
  Json::CharReaderBuilder reader;
  // #1607 residual (R20-05): pinned stackLimit + typed catch — the ledger is
  // a hand-editable sidecar in this stack's threat model, and jsoncpp THROWS
  // (never returns false) once the nesting limit is exceeded; the refusal
  // must stay a typed parse issue, not a crash out of loadVerified.
  reader["stackLimit"] = 256;
  std::string errors;
  std::istringstream input( result.bytes );
  try
  {
    if ( !Json::parseFromStream( reader, input, &doc, &errors ) ) {
      ledger.issuesZh.push_back( "完成账本解析失败: " + errors );
      return ledger;
    }
  }
  catch ( const std::exception &e )
  {
    ledger.issuesZh.push_back( "完成账本解析失败: " + std::string( e.what() ) );
    return ledger;
  }
  ledger = fromJson( doc );
  if ( !ledger.ok )
    return ledger;
  // Evidence re-verification: an entry whose session still exists must
  // still carry the recorded verdict digest. A session that moved on
  // (re-validated, re-stamped) or vanished demotes the entry — completion
  // is a live claim about persisted evidence, not a historical note.
  std::vector<LabCompletionEntry> verified;
  verified.reserve( ledger.completed.size() );
  for ( const auto &entry : ledger.completed ) {
    const LabSessionState session = LabSessionState::loadFromFile( entry.sessionRef );
    if ( !session.ok ) {
      ledger.issuesZh.push_back( "完成条目的会话不可读，暂不计入: " + entry.labId );
      continue;
    }
    const VerdictStamp stamp = parseVerdictStamp( session.verdictArtifactStamp );
    if ( !stamp.parsed || stamp.summaryDigest != entry.verdictDigest ) {
      ledger.issuesZh.push_back( "完成条目与会话判定不一致，暂不计入: " + entry.labId );
      continue;
    }
    // R30-T07 G5: the completion OBJECT must match its evidence. A valid A
    // session spliced into a B entry is not a B completion.
    if ( session.labId != entry.labId ) {
      ledger.issuesZh.push_back(
        "完成条目与会话的 lab 不一致（错接判定），暂不计入: " + entry.labId );
      continue;
    }
    if ( !entry.sessionId.empty() && session.sessionId != entry.sessionId ) {
      ledger.issuesZh.push_back(
        "完成条目与会话标识不一致，暂不计入: " + entry.labId );
      continue;
    }
    if ( !entry.courseId.empty() && session.courseId != entry.courseId ) {
      ledger.issuesZh.push_back(
        "完成条目与课程不一致，暂不计入: " + entry.labId );
      continue;
    }
    // Legacy migration: an entry written before course identity existed
    // adopts the course of its VERIFIED session instead of being cleared.
    // Upgrade never silently empties a student's real completion evidence.
    LabCompletionEntry adopted = entry;
    if ( adopted.courseId.empty() )
      adopted.courseId = session.courseId;
    if ( adopted.sessionId.empty() )
      adopted.sessionId = session.sessionId;
    if ( !courseId.empty() && adopted.courseId != courseId ) {
      ledger.issuesZh.push_back(
        "完成条目属于其他课程，不计入本课程进度: " + entry.labId );
      continue;
    }
    verified.push_back( adopted );
  }
  ledger.completed = std::move( verified );
  return ledger;
}

bool LabCompletionLedger::saveToFile( const std::string &path ) const
{
  if ( !ok ) return false;
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  ";
  writer["emitUTF8"] = true;
  const sicnu::platform::sidecar::WriteResult result =
    sicnu::platform::sidecar::write( { path, Json::writeString( writer, toJson() ), "" } );
  return static_cast<bool>( result );
}

Json::Value LabCompletionLedger::toProgressDoc( const std::string &courseId ) const
{
  Json::Value doc( Json::objectValue );
  doc["schema"] = "sicnu.curriculum.progress/1";
  Json::Value done( Json::objectValue );
  std::set<std::string> usedLabIds;
  for ( const auto &entry : completed ) {
    // R30-T07 R1 (P1-7): when a course is named, ONLY entries of that exact
    // course count. An unscoped legacy entry belongs to no named course, so
    // it must not leak into every course's progress. loadVerified() adopts
    // the verified session's course before this projection runs.
    if ( !courseId.empty() ) {
      if ( entry.courseId != courseId )
        continue;
    }
    Json::Value e( Json::objectValue );
    e["evidence"] = entry.sessionRef;
    // Caller-injected by contract; the ledger reads no clock.
    e["completed_at_iso"] = "";
    // Two courses sharing a lab id must not overwrite each other's progress
    // entry in the unscoped ("all") projection.
    const bool first = usedLabIds.insert( entry.labId ).second;
    done[first ? entry.labId : entryKey( entry.labId, entry.courseId )] = e;
  }
  doc["completed"] = done;
  return doc;
}

} // namespace sicnu::teaching
