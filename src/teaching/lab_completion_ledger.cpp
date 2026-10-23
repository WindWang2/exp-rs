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
    if ( entry.labId == session.labId ) {
      // Latest verified verdict wins; the entry keeps its evidence chain.
      entry.sessionRef = sessionRef;
      entry.verdictDigest = stamp.summaryDigest;
      entry.artifactStamp = session.verdictArtifactStamp;
      return true;
    }
  }
  LabCompletionEntry entry;
  entry.labId = session.labId;
  entry.sessionRef = sessionRef;
  entry.verdictDigest = stamp.summaryDigest;
  entry.artifactStamp = session.verdictArtifactStamp;
  completed.push_back( entry );
  // Deterministic order: sorted by lab id.
  std::sort( completed.begin(), completed.end(),
             []( const LabCompletionEntry &a, const LabCompletionEntry &b ) {
               return a.labId < b.labId;
             } );
  return true;
}

Json::Value LabCompletionLedger::toJson() const
{
  Json::Value root( Json::objectValue );
  root["schema"] = schema;
  Json::Value done( Json::objectValue );
  for ( const auto &entry : completed ) {
    Json::Value e( Json::objectValue );
    e["session_ref"] = entry.sessionRef;
    e["verdict_digest"] = entry.verdictDigest;
    e["artifact_stamp"] = entry.artifactStamp;
    done[entry.labId] = e;
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
  const auto labIds = completed.getMemberNames();
  for ( const auto &labId : labIds ) {
    if ( labId.empty() ) {
      ledger.issuesZh.push_back( "completed 含空 lab id" );
      return ledger;
    }
    const Json::Value &e = completed[labId];
    if ( !e.isObject() ) {
      ledger.issuesZh.push_back( "完成条目必须是对象: " + labId );
      return ledger;
    }
    const auto entryKeys = e.getMemberNames();
    for ( const auto &k : entryKeys ) {
      if ( k != "session_ref" && k != "verdict_digest" && k != "artifact_stamp" ) {
        ledger.issuesZh.push_back( "完成条目未知字段: " + labId + "." + k );
        return ledger;
      }
    }
    LabCompletionEntry entry;
    entry.labId = labId;
    entry.sessionRef = strOf( e, "session_ref" );
    entry.verdictDigest = strOf( e, "verdict_digest" );
    entry.artifactStamp = strOf( e, "artifact_stamp" );
    if ( entry.sessionRef.empty() || entry.verdictDigest.size() != 64 ) {
      ledger.issuesZh.push_back( "完成条目缺证据链: " + labId );
      return ledger;
    }
    if ( !seenDigests.insert( entry.verdictDigest ).second ) {
      // Two labs claiming the same verdict document — one of them is a
      // copy-paste forgery; refuse to arbitrate, keep neither.
      ledger.issuesZh.push_back( "多个条目引用同一判定文档（拒绝仲裁）" );
      ledger.completed.clear();
      return ledger;
    }
    ledger.completed.push_back( entry );
  }
  std::sort( ledger.completed.begin(), ledger.completed.end(),
             []( const LabCompletionEntry &a, const LabCompletionEntry &b ) {
               return a.labId < b.labId;
             } );
  ledger.ok = true;
  return ledger;
}

LabCompletionLedger LabCompletionLedger::loadVerified( const std::string &ledgerPath )
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
  // must stay a typed parse issue, not a crash out of loadVerified. The
  // std::exception catch matches LabSessionState::deserialize's spelling
  // (Json::RuntimeError is-a std::exception; grader_json narrows to
  // Json::Exception — both are fail-closed, this file follows the
  // lab_session_state form it sits beside).
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
    verified.push_back( entry );
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

Json::Value LabCompletionLedger::toProgressDoc() const
{
  Json::Value doc( Json::objectValue );
  doc["schema"] = "sicnu.curriculum.progress/1";
  Json::Value done( Json::objectValue );
  for ( const auto &entry : completed ) {
    Json::Value e( Json::objectValue );
    e["evidence"] = entry.sessionRef;
    // Caller-injected by contract; the ledger reads no clock.
    e["completed_at_iso"] = "";
    done[entry.labId] = e;
  }
  doc["completed"] = done;
  return doc;
}

} // namespace sicnu::teaching
