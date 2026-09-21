// src/agent/harness/curriculum_progress.cpp
#include "agent/harness/curriculum_progress.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace sicnu::agent::harness {

namespace {

bool isNonEmptyString( const Json::Value &value )
{
    return value.isString() && !value.asString().empty();
}

} // namespace

const char *CurriculumProgress::kSchema = "sicnu.curriculum.progress/1";
const char *CurriculumProgress::kSummarySchema = "sicnu.curriculum.progress.summary/1";

std::string CurriculumProgressIssue::toString() const
{
    std::ostringstream out;
    out << "[" << code << "] " << path << ": " << message_zh;
    return out.str();
}

Json::Value CurriculumProgress::emptyDoc()
{
    Json::Value doc{ Json::objectValue };
    doc["schema"] = kSchema;
    doc["completed"] = Json::Value{ Json::objectValue };
    return doc;
}

std::vector<CurriculumProgressIssue> CurriculumProgress::validateDoc(
    const Json::Value &doc, const std::vector<std::string> &knownLabIds )
{
    std::vector<CurriculumProgressIssue> issues;
    if ( !doc.isObject() || !doc["schema"].isString() || doc["schema"].asString() != kSchema )
    {
        issues.push_back( { "progress_invalid_doc", "schema",
                            std::string( "进度文档 schema 必须是 \"" ) + kSchema + "\"。" } );
        return issues;
    }
    for ( const auto &key : doc.getMemberNames() )
        if ( key != "schema" && key != "student_note" && key != "completed" )
            issues.push_back( { "progress_invalid_doc", key,
                                "进度文档拒绝未知键 \"" + key + "\"。" } );

    const Json::Value completed = doc["completed"];
    if ( !completed.isObject() )
    {
        issues.push_back( { "progress_invalid_doc", "completed", "completed 必须是对象。" } );
        return issues;
    }
    const std::set<std::string> known( knownLabIds.begin(), knownLabIds.end() );
    for ( const auto &labId : completed.getMemberNames() )
    {
        const std::string at = "completed." + labId;
        if ( !knownLabIds.empty() && known.find( labId ) == known.end() )
        {
            issues.push_back( { "progress_unknown_lab", at,
                                "进度引用了课程中不存在的 lab \"" + labId + "\"。" } );
            continue;
        }
        const Json::Value entry = completed[labId];
        if ( !entry.isObject() )
        {
            issues.push_back( { "progress_missing_field", at,
                                "完成记录必须是 {evidence, completed_at_iso} 对象。" } );
            continue;
        }
        if ( !isNonEmptyString( entry["evidence"] ) )
            issues.push_back( { "progress_missing_field", at + ".evidence",
                                "完成记录必须带非空 evidence。" } );
        if ( !isNonEmptyString( entry["completed_at_iso"] ) )
            issues.push_back( { "progress_missing_field", at + ".completed_at_iso",
                                "完成记录必须带非空 completed_at_iso（由调用方注入，保证确定性）。" } );
    }
    return issues;
}

bool CurriculumProgress::markCompleted( Json::Value &doc, const std::string &labId,
                                        const std::string &evidence,
                                        const std::string &completedAtIso,
                                        const std::vector<std::string> &knownLabIds,
                                        std::vector<CurriculumProgressIssue> *issues )
{
    const auto fail = [&]( const std::string &code, const std::string &path,
                           const std::string &message ) {
        if ( issues ) issues->push_back( { code, path, message } );
        return false;
    };

    if ( !doc.isObject() || !doc["schema"].isString() || doc["schema"].asString() != kSchema )
        return fail( "progress_invalid_doc", "schema",
                     std::string( "进度文档 schema 必须是 \"" ) + kSchema + "\"。" );
    if ( labId.empty() ) return fail( "progress_unknown_lab", "lab_id", "lab_id 为空。" );
    if ( !knownLabIds.empty()
         && std::find( knownLabIds.begin(), knownLabIds.end(), labId ) == knownLabIds.end() )
        return fail( "progress_unknown_lab", "completed." + labId,
                     "进度引用了课程中不存在的 lab \"" + labId + "\"。" );
    if ( evidence.empty() )
        return fail( "progress_missing_field", "completed." + labId + ".evidence",
                     "evidence 必须非空。" );
    if ( completedAtIso.empty() )
        return fail( "progress_missing_field", "completed." + labId + ".completed_at_iso",
                     "completed_at_iso 必须非空（调用方注入，保证确定性）。" );

    if ( !doc["completed"].isObject() ) doc["completed"] = Json::Value{ Json::objectValue };

    Json::Value &entry = doc["completed"][labId];
    if ( entry.isObject() )
    {
        const bool sameEvidence = entry["evidence"].isString() && entry["evidence"].asString() == evidence;
        const bool sameTime =
            entry["completed_at_iso"].isString() && entry["completed_at_iso"].asString() == completedAtIso;
        if ( sameEvidence && sameTime ) return true;  // idempotent
        return fail( "progress_conflicting_evidence", "completed." + labId,
                     "lab \"" + labId + "\" 已有不同的完成证据；进度不覆盖，需人工裁决。" );
    }

    entry = Json::Value{ Json::objectValue };
    entry["evidence"] = evidence;
    entry["completed_at_iso"] = completedAtIso;
    return true;
}

std::vector<CurriculumProgress::ModuleStat> CurriculumProgress::moduleCompletion(
    const Json::Value &doc, const Json::Value &manifest )
{
    std::vector<ModuleStat> stats;
    if ( !doc.isObject() || !manifest.isObject() || !manifest["modules"].isArray() ) return stats;
    for ( const auto &module : manifest["modules"] )
    {
        ModuleStat stat;
        stat.moduleId = module["id"].isString() ? module["id"].asString() : "";
        if ( module["labs"].isArray() )
            for ( const auto &lab : module["labs"] )
            {
                if ( !lab.isObject() || !lab["lab_id"].isString() ) continue;
                ++stat.total;
                if ( doc["completed"].isObject() && doc["completed"].isMember( lab["lab_id"].asString() ) )
                    ++stat.done;
            }
        stats.push_back( stat );
    }
    return stats;
}

Json::Value CurriculumProgress::summary( const Json::Value &doc, const Json::Value &manifest,
                                         const std::vector<std::string> &knownLabIds )
{
    Json::Value out{ Json::objectValue };
    out["schema"] = kSummarySchema;

    const std::vector<CurriculumProgressIssue> issues = validateDoc( doc, knownLabIds );
    if ( !issues.empty() )
    {
        out["ok"] = false;
        Json::Value issueArray{ Json::arrayValue };
        for ( const auto &issue : issues )
        {
            Json::Value item{ Json::objectValue };
            item["code"] = issue.code;
            item["path"] = issue.path;
            item["message_zh"] = issue.message_zh;
            issueArray.append( item );
        }
        out["issues"] = issueArray;
        out["overall_percent"] = 0;
        return out;
    }

    if ( !manifest.isObject() || !manifest["modules"].isArray() )
    {
        out["ok"] = false;
        Json::Value issueArray{ Json::arrayValue };
        Json::Value item{ Json::objectValue };
        item["code"] = "manifest_unavailable";
        item["path"] = "manifest";
        item["message_zh"] = "课程清单不可用，无法投影进度。";
        issueArray.append( item );
        out["issues"] = issueArray;
        out["overall_percent"] = 0;
        return out;
    }

    out["ok"] = true;

    // Overall completion dedupes by lab id; module stats count per module.
    std::set<std::string> allLabs;
    std::set<std::string> doneLabs;
    if ( doc["completed"].isObject() )
        for ( const auto &labId : doc["completed"].getMemberNames() ) doneLabs.insert( labId );

    Json::Value moduleArray{ Json::arrayValue };
    const auto stats = moduleCompletion( doc, manifest );
    size_t statIndex = 0;
    for ( const auto &module : manifest["modules"] )
    {
        const ModuleStat &stat = stats[statIndex++];
        for ( const auto &lab : module["labs"] )
            if ( lab.isObject() && lab["lab_id"].isString() )
                allLabs.insert( lab["lab_id"].asString() );

        Json::Value moduleOut{ Json::objectValue };
        moduleOut["module_id"] = stat.moduleId;
        moduleOut["title_zh"] = module["title_zh"];
        moduleOut["done"] = stat.done;
        moduleOut["total"] = stat.total;
        const int percent = stat.total > 0 ? static_cast<int>( std::floor( stat.done * 100.0 / stat.total ) ) : 0;
        moduleOut["percent"] = percent;
        Json::Value missing{ Json::arrayValue };
        if ( module["labs"].isArray() )
            for ( const auto &lab : module["labs"] )
                if ( lab.isObject() && lab["lab_id"].isString()
                     && doneLabs.find( lab["lab_id"].asString() ) == doneLabs.end() )
                    missing.append( lab["lab_id"].asString() );
        moduleOut["missing_lab_ids"] = missing;
        moduleArray.append( moduleOut );
    }

    const int overall = allLabs.empty()
                            ? 0
                            : static_cast<int>( std::floor( static_cast<double>( doneLabs.size() * 100 )
                                                            / static_cast<double>( allLabs.size() ) ) );
    out["overall_percent"] = overall;

    Json::Value completedIds{ Json::arrayValue };
    for ( const auto &labId : allLabs )
        if ( doneLabs.count( labId ) ) completedIds.append( labId );
    out["completed_lab_ids"] = completedIds;
    out["modules"] = moduleArray;
    return out;
}

std::string CurriculumProgress::serialize( const Json::Value &doc )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    builder["commentStyle"] = "None";
    builder["precision"] = 17;
    builder["validateUtf8"] = false;
    // jsoncpp emits object members in sorted key order — pinned determinism.
    return Json::writeString( builder, doc );
}

} // namespace sicnu::agent::harness
