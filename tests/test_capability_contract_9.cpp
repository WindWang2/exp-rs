/***************************************************************************
 * test_capability_contract_9.cpp
 *
 * Contract Platform 9.0 (M4) — capability knowledge drift floors:
 *
 *   1. operator floor: every registered operator is reachable from the
 *      agent surface (tool catalog) or explicitly allow-listed as
 *      low-level (io:/gdal:/opencv: plumbing);
 *   2. tool-schema projection: for operator-backed tools, the tool
 *      inputSchema parameter set ⊇ the operator schema parameter set —
 *      the MCP/agent surface cannot lag the operator contract;
 *   3. recipe references: every recipe step operator_id resolves in the
 *      live registry (phantom capability direction);
 *   4. capability entries: every non-family capability entry id resolves
 *      in the registry, and every registry operator named by an entry
 *      still exists (both drift directions);
 *   5. tool catalog hygiene: no duplicate tool names (ambiguous surface).
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include <agent/harness/capability_knowledge.h>
#include <agent/tool_catalog/agent_tool_catalog.h>
#include <agent/tool_catalog/agent_tool.h>
#include <operators/framework/rs_operator.h>
#include <operators/framework/rs_operator_registry.h>
#include <operators/rs/rs_operators_init.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

const char *kSourceDir = CMAKE_SOURCE_DIR;

struct AllowEntry
{
    const char *op;
    const char *reason;
};

/// Operators that intentionally have no curated agent tool surface
/// (low-level plumbing exposed through composite flows only).
const std::vector<AllowEntry> kOperatorsWithoutTool = {
    // { "op:id", "reason" }
};

bool allowed( const std::vector<AllowEntry> &list, const std::string &op )
{
    return std::any_of( list.begin(), list.end(),
                        [&]( const AllowEntry &e ) { return op == e.op; } );
}

std::set<std::string> operatorNames()
{
    sicnu::operators::rs::initBuiltinRsOperators();
    const auto names =
        sicnu::operators::RSOperatorRegistry::instance().operatorNames();
    return { names.begin(), names.end() };
}

/// Static tool-id universe from the agent sources: provider name() overrides
/// plus the data-platform tool table. Together with the live catalog this
/// is the closed vocabulary capability entries may reference.
std::set<std::string> toolIdsFromAgentSources()
{
    std::set<std::string> ids;
    const std::string root =
        std::string( kSourceDir ) + "/src/agent";
    std::error_code ec;
    for ( auto it = std::filesystem::recursive_directory_iterator(
              root, std::filesystem::directory_options::skip_permission_denied,
              ec );
          it != std::filesystem::recursive_directory_iterator();
          it.increment( ec ) )
    {
        if ( ec )
            break;
        if ( !it->is_regular_file( ec ) ||
             it->path().extension() != ".cpp" )
            continue;
        std::ifstream in( it->path() );
        const std::string src{
            std::istreambuf_iterator<char>( in ),
            std::istreambuf_iterator<char>{} };
        // provider tool name() overrides: return "ns:tool";
        static const std::regex reName(
            R"re(name\(\)\s*(?:const\s*)?override\s*\{\s*return\s*"([a-z]+:[a-z_0-9]+)")re" );
        // data-platform style tables: { "ns:tool",
        static const std::regex reTable(
            R"re(\{\s*"([a-z]+:[a-z_0-9]+)",\s*
?\s*")re" );
        for ( const auto &re : { reName, reTable } )
        {
            auto begin = std::sregex_iterator( src.begin(), src.end(), re );
            for ( ; begin != std::sregex_iterator(); ++begin )
                ids.insert( ( *begin )[1].str() );
        }
    }
    return ids;
}

std::set<std::string> capabilityEntryIds()
{
    std::set<std::string> ids;
    QDir dir( QStringLiteral( "%1/data/agent/capabilities" )
                  .arg( QStringLiteral( CMAKE_SOURCE_DIR ) ) );
    const auto files = dir.entryList( QStringList{ QStringLiteral( "*.json" ) },
                                      QDir::Files, QDir::Name );
    for ( const auto &f : files )
    {
        QFile file( dir.filePath( f ) );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        QJsonParseError pe;
        const auto doc = QJsonDocument::fromJson( file.readAll(), &pe );
        REQUIRE( pe.error == QJsonParseError::NoError );
        for ( const auto &e : doc.array() )
        {
            const QString id =
                e.toObject()[QStringLiteral( "id" )].toString();
            if ( !id.isEmpty() )
                ids.insert( id.toStdString() );
        }
    }
    return ids;
}

/// All recipe JSON files under data/agent/recipes.
std::vector<QJsonDocument> recipeDocuments()
{
    std::vector<QJsonDocument> docs;
    QDir dir( QStringLiteral( "%1/data/agent/recipes" )
                  .arg( QStringLiteral( CMAKE_SOURCE_DIR ) ) );
    const auto files = dir.entryList( QStringList{ QStringLiteral( "*.json" ) },
                                      QDir::Files, QDir::Name );
    for ( const auto &f : files )
    {
        QFile file( dir.filePath( f ) );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        QJsonParseError pe;
        auto doc = QJsonDocument::fromJson( file.readAll(), &pe );
        REQUIRE( pe.error == QJsonParseError::NoError );
        docs.push_back( std::move( doc ) );
    }
    REQUIRE( docs.size() >= 3 );
    return docs;
}

} // namespace

TEST_CASE( "Operator floor: every registered operator is agent-reachable "
           "(allow-listed)",
           "[contracts9][capability]" )
{
    const auto ops = operatorNames();
    REQUIRE( ops.size() >= 100 );

    auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
    catalog.initializeDefaults();
    const auto tools = catalog.listTools();
    REQUIRE( tools.size() >= 50 );
    std::set<std::string> toolNames;
    for ( const auto &t : tools )
        toolNames.insert( t.name );

    for ( const auto &op : ops )
    {
        INFO( "operator without tool surface: " << op );
        CHECK( ( toolNames.count( op ) == 1 ||
                 allowed( kOperatorsWithoutTool, op ) ) );
    }
}

TEST_CASE( "Tool-schema projection: operator-backed tools expose every "
           "declared parameter",
           "[contracts9][capability][projection]" )
{
    auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
    catalog.initializeDefaults();
    const auto tools = catalog.listTools();
    std::map<std::string, Json::Value> toolSchemas;
    for ( const auto &t : tools )
        toolSchemas[t.name] = t.inputSchema;

    const auto ops = operatorNames();
    int compared = 0;
    for ( const auto &op : ops )
    {
        if ( !toolSchemas.count( op ) )
            continue;
        ++compared;
        auto instance =
            sicnu::operators::RSOperatorRegistry::instance().create( op );
        REQUIRE( instance != nullptr );
        const Json::Value schema = instance->schema();
        const Json::Value properties = schema["properties"];

        const Json::Value toolProps = toolSchemas[op]["properties"];
        for ( const auto &key : properties.getMemberNames() )
        {
            INFO( op << ": schema param '" << key
                     << "' missing from tool inputSchema" );
            CHECK( toolProps.isMember( key ) );
        }
    }
    INFO( "operator-backed tools compared: " << compared );
    CHECK( compared >= 20 );
}

TEST_CASE( "Recipe references resolve in the live registry",
           "[contracts9][capability][recipes]" )
{
    const auto ops = operatorNames();
    int stepsChecked = 0;
    for ( const auto &doc : recipeDocuments() )
    {
        const auto recipeId =
            doc.object()[QStringLiteral( "recipe_id" )].toString();
        const auto steps = doc.object()[QStringLiteral( "steps" )].toArray();
        for ( const auto &step : steps )
        {
            const QString opId =
                step.toObject()[QStringLiteral( "operator_id" )].toString();
            if ( opId.isEmpty() )
                continue;
            ++stepsChecked;
            INFO( "recipe "
              << recipeId.toStdString()
              << " references missing operator " << opId.toStdString() );
            CHECK( ops.count( opId.toStdString() ) == 1 );
        }
    }
    INFO( "recipe steps checked: " << stepsChecked );
    CHECK( stepsChecked >= 5 );
}

TEST_CASE( "Capability entries resolve in the registry or the tool catalog",
           "[contracts9][capability][knowledge]" )
{
    const auto ops = operatorNames();
    auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
    catalog.initializeDefaults();
    std::set<std::string> toolNames;
    for ( const auto &t : catalog.listTools() )
        toolNames.insert( t.name );
    for ( const auto &id : toolIdsFromAgentSources() )
        toolNames.insert( id );

    const auto entries = capabilityEntryIds();
    REQUIRE( entries.size() >= 20 );

    for ( const auto &id : entries )
    {
        if ( id.rfind( "family:", 0 ) == 0 )
            continue;
        INFO( "capability entry does not resolve: " << id );
        CHECK( ( ops.count( id ) == 1 || toolNames.count( id ) == 1 ) );
    }
}

TEST_CASE( "Tool catalog has no duplicate names (unambiguous surface)",
           "[contracts9][capability]" )
{
    auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
    catalog.initializeDefaults();
    const auto duplicates = catalog.findDuplicateNames();
    for ( const auto &d : duplicates )
        INFO( "duplicate tool name: " << d );
    CHECK( duplicates.empty() );
}
