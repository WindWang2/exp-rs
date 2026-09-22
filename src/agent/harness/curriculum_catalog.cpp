// src/agent/harness/curriculum_catalog.cpp
#include "agent/harness/curriculum_catalog.h"

#include "agent/harness/curriculum_progress.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace sicnu::agent::harness {

namespace {

constexpr const char *kSchema = "sicnu.curriculum/1";

const char *const kTopLevelKeys[] = { "schema",        "id",           "title",
                                      "title_zh",      "audience_zh",  "note_zh",
                                      "modules",       "forward_references" };

const char *const kModuleKeys[] = { "id",
                                    "index",
                                    "title",
                                    "title_zh",
                                    "summary_zh",
                                    "learning_outcomes",
                                    "prerequisite_modules",
                                    "estimated_effort_minutes",
                                    "optional",
                                    "labs" };

const char *const kLabKeys[] = { "lab_id",
                                 "role",
                                 "estimated_effort_minutes",
                                 "required_data_packs",
                                 "teacher_notes" };

const char *const kTeacherNoteKeys[] = { "objectives_zh", "common_mistakes_zh",
                                         "grading_hook_zh" };

const char *const kMistakeKeys[] = { "mistake_zh", "why_zh", "check_zh" };

const char *const kForwardKeys[] = { "capability", "reason_zh", "wiring" };

bool isAllowedKey( const std::string &key, const char *const *allowed, size_t count )
{
    for ( size_t i = 0; i < count; ++i )
        if ( key == allowed[i] ) return true;
    return false;
}

void checkKeys( const Json::Value &object, const char *const *allowed, size_t count,
                const std::string &path, std::vector<CurriculumIssue> &issues )
{
    if ( !object.isObject() ) return;
    for ( const auto &key : object.getMemberNames() )
    {
        if ( !isAllowedKey( key, allowed, count ) )
            issues.push_back( { "unknown_key", path + "." + key,
                                "未知键 \"" + key + "\"：curriculum 清单拒绝未知字段，杜绝静默漂移。" } );
    }
}

bool isNonEmptyString( const Json::Value &value )
{
    return value.isString() && !value.asString().empty();
}

bool isPositiveInt( const Json::Value &value )
{
    return value.isIntegral() && value.asInt64() > 0;
}

bool moduleIdPatternOk( const std::string &id )
{
    // ^m[0-9]{2}_[a-z][a-z0-9_]*$
    if ( id.size() < 5 || id[0] != 'm' ) return false;
    if ( !std::isdigit( static_cast<unsigned char>( id[1] ) ) ||
         !std::isdigit( static_cast<unsigned char>( id[2] ) ) || id[3] != '_' )
        return false;
    const char head = id[4];
    if ( !( ( head >= 'a' && head <= 'z' ) ) ) return false;
    for ( size_t i = 4; i < id.size(); ++i )
    {
        const char c = id[i];
        const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_';
        if ( !ok ) return false;
    }
    return true;
}

Json::Value parseJsonFile( const std::filesystem::path &path, std::string *error )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
    {
        if ( error ) *error = "cannot open " + path.string();
        return {};
    }
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    Json::Value root;
    std::string errs;
    const bool ok = static_cast<bool>( Json::parseFromStream( builder, in, &root, &errs ) );
    if ( !ok )
    {
        if ( error ) *error = errs;
        return {};
    }
    return root;
}

std::filesystem::path envOrEmpty( const char *name )
{
    const char *value = std::getenv( name );
    return ( value && value[0] ) ? std::filesystem::path( value ) : std::filesystem::path();
}

std::filesystem::path sourceDirFallback()
{
#if defined( SICNU_SOURCE_DIR )
#define SICNU_STR2( x ) #x
#define SICNU_STR( x ) SICNU_STR2( x )
    return std::filesystem::path( SICNU_STR( SICNU_SOURCE_DIR ) );
#undef SICNU_STR
#undef SICNU_STR2
#else
    return {};
#endif
}

std::filesystem::path defaultDataRoot( const char *subdir )
{
    if ( auto dir = envOrEmpty( "SICNU_CURRICULUM_DIR" ); !dir.empty() && std::string( subdir ) == "curriculum" )
        return dir;
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path( ec );
    if ( !ec )
    {
        const std::filesystem::path candidate = cwd / "data" / subdir;
        if ( std::filesystem::is_directory( candidate, ec ) ) return candidate;
    }
    if ( const std::filesystem::path source = sourceDirFallback(); !source.empty() )
        return source / "data" / subdir;
    return cwd / "data" / subdir;
}

} // namespace

std::string CurriculumIssue::toString() const
{
    std::ostringstream out;
    out << "[" << code << "] " << path << ": " << message_zh;
    return out.str();
}

CurriculumCatalog &CurriculumCatalog::instance()
{
    static CurriculumCatalog catalog;
    return catalog;
}

void CurriculumCatalog::setDirectory( const std::string &directory )
{
    mDirectory = directory;
}

std::string CurriculumCatalog::directory() const
{
    return mDirectory.empty() ? defaultDataRoot( "curriculum" ).string() : mDirectory;
}

void CurriculumCatalog::setPaths( const CurriculumPaths &paths )
{
    mPaths = paths;
}

CurriculumPaths CurriculumCatalog::paths() const
{
    return effectivePaths();
}

CurriculumPaths CurriculumCatalog::effectivePaths() const
{
    CurriculumPaths effective = mPaths;
    if ( effective.labsDir.empty() ) effective.labsDir = defaultDataRoot( "labs" ).string();
    if ( effective.packsDir.empty() ) effective.packsDir = ( std::filesystem::path( effective.labsDir ) / "packs" ).string();
    return effective;
}

std::string resolveLabReference( const std::string &labId, const CurriculumPaths &paths )
{
    if ( labId.empty() ) return "unknown";
    std::error_code ec;

    // 1. direct labspec document
    const std::filesystem::path labspec = std::filesystem::path( paths.labsDir ) / ( labId + ".lab.json" );
    if ( std::filesystem::is_regular_file( labspec, ec ) )
    {
        std::string error;
        const Json::Value doc = parseJsonFile( labspec, &error );
        if ( error.empty() && doc.isObject() && doc["id"].isString() && doc["id"].asString() == labId
             && doc["spec_version"].isIntegral()
             && ( doc["spec_version"].asInt() == 1 || doc["spec_version"].asInt() == 2 )
             && doc["steps"].isArray() )
            return "labspec";
        // A labspec-looking file that fails the shallow probe stays unresolved
        // — the strict loader remains the validity authority; we only route.
    }

    // 2. lab-registry canonical / alias
    const std::filesystem::path registryPath = std::filesystem::path( paths.labsDir ) / "lab-registry.json";
    if ( std::filesystem::is_regular_file( registryPath, ec ) )
    {
        std::string error;
        const Json::Value registry = parseJsonFile( registryPath, &error );
        const Json::Value canonical = error.empty() ? registry["canonical"] : Json::Value();
        if ( canonical.isObject() )
        {
            for ( const auto &entryId : canonical.getMemberNames() )
            {
                const Json::Value entry = canonical[entryId];
                bool matches = ( entryId == labId );
                if ( !matches && entry["aliases"].isArray() )
                    for ( const auto &alias : entry["aliases"] )
                        if ( alias.isString() && alias.asString() == labId ) matches = true;
                if ( !matches ) continue;
                if ( entry["source"].isString() )
                {
                    const std::filesystem::path source =
                        std::filesystem::path( paths.labsDir ) / std::filesystem::path( entry["source"].asString() ).filename();
                    if ( std::filesystem::is_regular_file( source, ec ) )
                    {
                        std::string sourceError;
                        const Json::Value sourceDoc = parseJsonFile( source, &sourceError );
                        if ( sourceError.empty() && sourceDoc.isObject() && sourceDoc["id"].isString() )
                            return "registry";
                    }
                }
            }
        }
    }

    // 3. explicitly external (owned by another track; read-only reference)
    if ( std::filesystem::is_regular_file( registryPath, ec ) )
    {
        std::string error;
        const Json::Value registry = parseJsonFile( registryPath, &error );
        if ( error.empty() && registry["out_of_scope"].isObject()
             && registry["out_of_scope"].isMember( labId ) )
            return "external";
    }

    return "unknown";
}

void validateCurriculumManifest( const Json::Value &manifest, const CurriculumPaths &paths,
                                 std::vector<CurriculumIssue> &issues )
{
    if ( !manifest.isObject() )
    {
        issues.push_back( { "manifest_unparseable", "",
                            "清单根必须是 JSON 对象。" } );
        return;
    }

    if ( !manifest["schema"].isString() || manifest["schema"].asString() != kSchema )
    {
        issues.push_back( { "unknown_schema", "schema",
                            std::string( "schema 必须是 \"" ) + kSchema + "\"。" } );
        return;  // everything downstream depends on the schema identity
    }

    checkKeys( manifest, kTopLevelKeys, std::size( kTopLevelKeys ), "", issues );

    if ( !isNonEmptyString( manifest["id"] ) )
        issues.push_back( { "missing_field", "id", "缺少非空 id。" } );
    if ( !isNonEmptyString( manifest["title"] ) )
        issues.push_back( { "missing_field", "title", "缺少非空 title。" } );
    if ( !isNonEmptyString( manifest["title_zh"] ) )
        issues.push_back( { "missing_field", "title_zh", "缺少非空 title_zh。" } );

    const Json::Value modules = manifest["modules"];
    if ( !modules.isArray() || modules.empty() )
    {
        issues.push_back( { "missing_field", "modules", "modules 必须是非空数组。" } );
        return;
    }

    // Collect ids first so prerequisite references can be checked.
    std::set<std::string> moduleIds;
    for ( const auto &module : modules )
        if ( module.isObject() && module["id"].isString() ) moduleIds.insert( module["id"].asString() );

    std::map<std::string, int> indexSeen;
    std::set<std::string> idSeen;
    size_t moduleIndex = 0;
    for ( const auto &module : modules )
    {
        const std::string fallbackAt = "modules[" + std::to_string( moduleIndex++ ) + "]";
        const std::string idForPath =
            ( module.isObject() && module["id"].isString() && !module["id"].asString().empty() )
                ? module["id"].asString()
                : fallbackAt;
        const std::string at = "modules[" + idForPath + "]";
        if ( !module.isObject() )
        {
            issues.push_back( { "missing_field", at, "module 必须是对象。" } );
            continue;
        }
        checkKeys( module, kModuleKeys, std::size( kModuleKeys ), at, issues );

        const std::string id = module["id"].isString() ? module["id"].asString() : "";
        if ( !moduleIdPatternOk( id ) )
        {
            issues.push_back( { "missing_field", at + ".id",
                                "module id 必须匹配 ^m[0-9]{2}_[a-z][a-z0-9_]*$。" } );
        }
        else if ( !idSeen.insert( id ).second )
        {
            issues.push_back( { "duplicate_module_id", at + ".id",
                                "module id \"" + id + "\" 重复。" } );
        }

        if ( !module["index"].isIntegral() || module["index"].asInt64() < 1 )
        {
            issues.push_back( { "missing_field", at + ".index", "index 必须是 ≥1 的整数。" } );
        }
        else
        {
            const int64_t index = module["index"].asInt64();
            if ( !indexSeen.emplace( std::to_string( index ), 1 ).second )
            {
                issues.push_back( { "duplicate_module_index", at + ".index",
                                    "module index 重复：" + std::to_string( index ) + "。" } );
            }
        }

        if ( !isNonEmptyString( module["title"] ) )
            issues.push_back( { "missing_field", at + ".title", "缺少非空 title。" } );
        if ( !isNonEmptyString( module["title_zh"] ) )
            issues.push_back( { "missing_field", at + ".title_zh", "缺少非空 title_zh。" } );
        if ( !isNonEmptyString( module["summary_zh"] ) )
            issues.push_back( { "missing_field", at + ".summary_zh", "缺少非空 summary_zh。" } );

        const Json::Value outcomes = module["learning_outcomes"];
        if ( !outcomes.isArray() || outcomes.empty() )
        {
            issues.push_back( { "empty_learning_outcomes", at + ".learning_outcomes",
                                "每个模块至少声明一条学习成果。" } );
        }
        else
        {
            size_t outcomeIndex = 0;
            for ( const auto &outcome : outcomes )
            {
                if ( !isNonEmptyString( outcome ) )
                    issues.push_back( { "missing_field",
                                        at + ".learning_outcomes[" + std::to_string( outcomeIndex ) + "]",
                                        "学习成果必须是非空字符串。" } );
                ++outcomeIndex;
            }
        }

        if ( module.isMember( "prerequisite_modules" ) && !module["prerequisite_modules"].isArray() )
            issues.push_back( { "missing_field", at + ".prerequisite_modules",
                                "prerequisite_modules 必须是数组。" } );
        size_t prereqIndex = 0;
        if ( module["prerequisite_modules"].isArray() )
            for ( const auto &prereq : module["prerequisite_modules"] )
            {
                const std::string path = at + ".prerequisite_modules["
                    + ( prereq.isString() ? prereq.asString()
                                          : std::to_string( prereqIndex ) ) + "]";
                if ( !prereq.isString() || moduleIds.find( prereq.asString() ) == moduleIds.end() )
                    issues.push_back( { "unknown_module_reference", path,
                                        "先修模块 \"" + prereq.asString() + "\" 不存在。" } );
                ++prereqIndex;
            }

        if ( module.isMember( "estimated_effort_minutes" ) && !isPositiveInt( module["estimated_effort_minutes"] ) )
            issues.push_back( { "nonpositive_effort", at + ".estimated_effort_minutes",
                                "estimated_effort_minutes 必须是 ≥1 的整数。" } );

        const Json::Value labs = module["labs"];
        if ( !labs.isArray() || labs.empty() )
        {
            issues.push_back( { "missing_field", at + ".labs", "每个模块至少引用一个 lab。" } );
            continue;
        }
        size_t labIndex = 0;
        for ( const auto &lab : labs )
        {
            const std::string labFallbackAt = at + ".labs[" + std::to_string( labIndex++ ) + "]";
            const std::string labIdForPath =
                ( lab.isObject() && lab["lab_id"].isString() && !lab["lab_id"].asString().empty() )
                    ? lab["lab_id"].asString()
                    : labFallbackAt;
            const std::string labAt = at + ".labs[" + labIdForPath + "]";
            if ( !lab.isObject() )
            {
                issues.push_back( { "missing_field", labAt, "lab 引用必须是对象。" } );
                continue;
            }
            checkKeys( lab, kLabKeys, std::size( kLabKeys ), labAt, issues );

            const std::string labId = lab["lab_id"].isString() ? lab["lab_id"].asString() : "";
            if ( labId.empty() )
            {
                issues.push_back( { "missing_field", labAt + ".lab_id", "缺少 lab_id。" } );
            }
            else
            {
                const std::string role = lab["role"].isString() ? lab["role"].asString() : "";
                if ( role != "core" && role != "optional" && role != "external" )
                {
                    issues.push_back( { "invalid_lab_role", labAt + ".role",
                                        "role 必须是 core | optional | external。" } );
                }

                if ( !lab.isMember( "estimated_effort_minutes" ) )
                    issues.push_back( { "missing_field", labAt + ".estimated_effort_minutes",
                                        "lab 引用必须声明学时。" } );
                else if ( !isPositiveInt( lab["estimated_effort_minutes"] ) )
                    issues.push_back( { "nonpositive_effort", labAt + ".estimated_effort_minutes",
                                        "estimated_effort_minutes 必须是 ≥1 的整数。" } );

                if ( !labId.empty() && ( role == "core" || role == "optional" ) )
                {
                    const std::string resolution = resolveLabReference( labId, paths );
                    if ( resolution != "labspec" && resolution != "registry" )
                        issues.push_back( { "unknown_lab_reference", labAt + ".lab_id",
                                            "lab \"" + labId + "\" 无法经 labspec 或 lab-registry 解析。" } );
                }
                else if ( !labId.empty() && role == "external" )
                {
                    // External labs must be declared out_of_scope by the registry —
                    // the owning track's territory, referenced read-only.
                    std::error_code ec;
                    const std::filesystem::path registryPath =
                        std::filesystem::path( paths.labsDir ) / "lab-registry.json";
                    bool declared = false;
                    if ( std::filesystem::is_regular_file( registryPath, ec ) )
                    {
                        std::string error;
                        const Json::Value registry = parseJsonFile( registryPath, &error );
                        declared = error.empty() && registry["out_of_scope"].isObject()
                                   && registry["out_of_scope"].isMember( labId );
                    }
                    if ( !declared )
                        issues.push_back( { "undeclared_external_lab", labAt + ".lab_id",
                                            "external lab \"" + labId
                                                + "\" 未在 lab-registry 的 out_of_scope 中声明。" } );
                }

                if ( lab.isMember( "required_data_packs" ) && !lab["required_data_packs"].isArray() )
                    issues.push_back( { "missing_field", labAt + ".required_data_packs",
                                        "required_data_packs 必须是数组。" } );
                size_t packIndex = 0;
                if ( lab["required_data_packs"].isArray() )
                    for ( const auto &pack : lab["required_data_packs"] )
                    {
                        const std::string packAt = labAt + ".required_data_packs["
                            + ( pack.isString() && !pack.asString().empty()
                                    ? pack.asString()
                                    : std::to_string( packIndex ) ) + "]";
                        ++packIndex;
                        if ( !pack.isString() || pack.asString().empty() )
                        {
                            issues.push_back( { "unknown_data_pack", packAt, "pack 名必须是非空字符串。" } );
                            continue;
                        }
                        std::error_code ec;
                        const std::filesystem::path packPath =
                            std::filesystem::path( paths.packsDir ) / ( pack.asString() + ".pack.json" );
                        bool present = false;
                        if ( std::filesystem::is_regular_file( packPath, ec ) )
                        {
                            std::string error;
                            const Json::Value packDoc = parseJsonFile( packPath, &error );
                            present = error.empty() && packDoc["schema_version"].isString()
                                      && packDoc["schema_version"].asString() == "sicnu.lab-pack/1";
                        }
                        if ( !present )
                            issues.push_back( { "unknown_data_pack", packAt,
                                                "数据包 \"" + pack.asString() + "\" 不存在或不是 sicnu.lab-pack/1。" } );
                    }

                if ( lab.isMember( "teacher_notes" ) && !lab["teacher_notes"].isObject() )
                    issues.push_back( { "missing_field", labAt + ".teacher_notes",
                                        "teacher_notes 必须是对象。" } );
                if ( lab["teacher_notes"].isObject() )
                {
                    checkKeys( lab["teacher_notes"], kTeacherNoteKeys, std::size( kTeacherNoteKeys ),
                               labAt + ".teacher_notes", issues );
                    size_t mistakeIndex = 0;
                    if ( lab["teacher_notes"]["common_mistakes_zh"].isArray() )
                        for ( const auto &mistake : lab["teacher_notes"]["common_mistakes_zh"] )
                        {
                            checkKeys( mistake, kMistakeKeys, std::size( kMistakeKeys ),
                                       labAt + ".teacher_notes.common_mistakes_zh["
                                           + std::to_string( mistakeIndex ) + "]",
                                       issues );
                            ++mistakeIndex;
                        }
                }
            }
        }
    }

    // forward_references: honest declarations only; no capability probing here.
    size_t forwardIndex = 0;
    if ( manifest.isMember( "forward_references" ) && !manifest["forward_references"].isArray() )
        issues.push_back( { "missing_field", "forward_references",
                            "forward_references 必须是数组。" } );
    if ( manifest["forward_references"].isArray() )
        for ( const auto &forward : manifest["forward_references"] )
        {
            const std::string at = "forward_references[" + std::to_string( forwardIndex++ ) + "]";
            if ( !forward.isObject() )
            {
                issues.push_back( { "missing_field", at, "forward_reference 必须是对象。" } );
                continue;
            }
            checkKeys( forward, kForwardKeys, std::size( kForwardKeys ), at, issues );
            if ( !isNonEmptyString( forward["capability"] ) )
                issues.push_back( { "missing_field", at + ".capability", "缺少 capability。" } );
            if ( !isNonEmptyString( forward["reason_zh"] ) )
                issues.push_back( { "missing_field", at + ".reason_zh",
                                    "forward_reference 必须说明不可用原因。" } );
        }

    // Acyclic prerequisite DAG (Kahn). Only references between known ids count.
    std::map<std::string, int> indegree;
    std::map<std::string, std::vector<std::string>> edges;
    for ( const auto &id : moduleIds )
    {
        indegree[id] = 0;
        edges[id] = {};
    }
    for ( const auto &module : modules )
    {
        if ( !module.isObject() || !module["id"].isString() ) continue;
        const std::string id = module["id"].asString();
        if ( module["prerequisite_modules"].isArray() )
            for ( const auto &prereq : module["prerequisite_modules"] )
            {
                if ( !prereq.isString() ) continue;
                const std::string from = prereq.asString();
                if ( edges.count( from ) )
                {
                    edges[from].push_back( id );
                    ++indegree[id];
                }
            }
    }
    std::vector<std::string> ready;
    for ( const auto &[id, degree] : indegree )
        if ( degree == 0 ) ready.push_back( id );
    size_t visited = 0;
    while ( !ready.empty() )
    {
        const std::string current = ready.back();
        ready.pop_back();
        ++visited;
        for ( const auto &next : edges[current] )
            if ( --indegree[next] == 0 ) ready.push_back( next );
    }
    if ( visited != moduleIds.size() )
        issues.push_back( { "cyclic_prerequisites", "modules",
                            "prerequisite_modules 构成环；课程先修必须是 DAG。" } );
}

int CurriculumCatalog::reload()
{
    mLoaded = false;
    mManifest = Json::Value{ Json::nullValue };
    mIssues.clear();

    const std::filesystem::path dir = directory();
    std::error_code ec;
    if ( !std::filesystem::is_directory( dir, ec ) )
    {
        mIssues.push_back( { "manifest_unavailable", "directory",
                             "curriculum 目录不存在：" + dir.string() } );
        return 0;
    }

    std::vector<std::filesystem::path> manifests;
    for ( std::filesystem::directory_iterator it( dir, ec ), end; !ec && it != end; it.increment( ec ) )
        if ( it->path().extension() == ".json" )
        {
            const std::string stem = it->path().stem().string();
            if ( stem.rfind( ".curriculum" ) == stem.size() - 11 && stem.size() > 11 )
                manifests.push_back( it->path() );
        }
    if ( manifests.empty() )
    {
        mIssues.push_back( { "manifest_unavailable", "directory",
                             "curriculum 目录中没有 *.curriculum.json：" + dir.string() } );
        return 0;
    }
    std::sort( manifests.begin(), manifests.end() );  // deterministic pick

    std::string error;
    const Json::Value manifest = parseJsonFile( manifests.front(), &error );
    if ( !error.empty() )
    {
        mIssues.push_back( { "manifest_unparseable", manifests.front().filename().string(),
                             "清单 JSON 解析失败：" + error } );
        return 0;
    }

    validateCurriculumManifest( manifest, effectivePaths(), mIssues );
    mLoaded = true;
    if ( !mIssues.empty() ) return 0;

    mManifest = manifest;
    return static_cast<int>( manifest["modules"].size() );
}

bool CurriculumCatalog::loaded() const
{
    return mLoaded;
}

std::string CurriculumCatalog::status() const
{
    return ( mLoaded && mIssues.empty() && mManifest.isObject() ) ? "ok" : "unavailable";
}

const std::vector<CurriculumIssue> &CurriculumCatalog::issues() const
{
    return mIssues;
}

std::vector<std::string> CurriculumCatalog::loadProblems() const
{
    std::vector<std::string> problems;
    problems.reserve( mIssues.size() );
    for ( const auto &issue : mIssues ) problems.push_back( issue.toString() );
    return problems;
}

Json::Value CurriculumCatalog::manifest() const
{
    return mManifest;
}

std::vector<std::string> CurriculumCatalog::moduleIds() const
{
    std::vector<std::string> ids;
    if ( !mManifest["modules"].isArray() ) return ids;
    std::vector<std::pair<int64_t, std::string>> ordered;
    for ( const auto &module : mManifest["modules"] )
        if ( module.isObject() && module["id"].isString() && module["index"].isIntegral() )
            ordered.emplace_back( module["index"].asInt64(), module["id"].asString() );
    std::sort( ordered.begin(), ordered.end() );
    ids.reserve( ordered.size() );
    for ( const auto &[_, id] : ordered ) ids.push_back( id );
    return ids;
}

Json::Value CurriculumCatalog::module( const std::string &moduleId ) const
{
    if ( !mManifest["modules"].isArray() ) return {};
    for ( const auto &module : mManifest["modules"] )
        if ( module.isObject() && module["id"].isString() && module["id"].asString() == moduleId )
            return module;
    return {};
}

Json::Value CurriculumCatalog::labRef( const std::string &moduleId, const std::string &labId ) const
{
    const Json::Value moduleDoc = module( moduleId );
    if ( !moduleDoc["labs"].isArray() ) return {};
    for ( const auto &lab : moduleDoc["labs"] )
        if ( lab.isObject() && lab["lab_id"].isString() && lab["lab_id"].asString() == labId )
            return lab;
    return {};
}

std::vector<std::string> CurriculumCatalog::labIds() const
{
    std::vector<std::string> ids;
    if ( !mManifest["modules"].isArray() ) return ids;
    std::set<std::string> seen;
    for ( const auto &module : mManifest["modules"] )
    {
        if ( !module["labs"].isArray() ) continue;
        for ( const auto &lab : module["labs"] )
            if ( lab.isObject() && lab["lab_id"].isString() )
            {
                const std::string id = lab["lab_id"].asString();
                if ( seen.insert( id ).second ) ids.push_back( id );
            }
    }
    return ids;
}

std::string CurriculumCatalog::labResolution( const std::string &labId ) const
{
    return resolveLabReference( labId, effectivePaths() );
}

Json::Value CurriculumCatalog::progressFor( const Json::Value &progressDoc ) const
{
    return CurriculumProgress::summary( progressDoc, mManifest, labIds() );
}

} // namespace sicnu::agent::harness
