// src/agent/tool_catalog/surface_registry.cpp

#include "surface_registry.h"

#include "meta_protocol_tools.h"
#include "agent_tool_catalog.h"
#include "data_platform_tools.h"
#include "interaction_tool_registry.h"
#include "../env_flag.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace sicnu::agent::tool_catalog {

namespace {

/// The headless rule MCP tools/list has always applied (#701): GUI-only
/// interaction entries disappear when the process has no interaction
/// registry (or lacks the view:get_state probe tool).
bool headlessHidesGuiTools()
{
    auto &registry = sicnu::agent::InteractionToolRegistry::instance();
    return registry.toolCount() == 0 || registry.findTool( "view:get_state" ) == std::nullopt;
}

bool guiOnlyCatalogEntry( const AgentTool &tool )
{
    const std::string &id = tool.name;
    return id.rfind( "view:", 0 ) == 0 || id.rfind( "roi:", 0 ) == 0
        || id.rfind( "canvas:", 0 ) == 0 || id.rfind( "layer:", 0 ) == 0
        || id.rfind( "raster:", 0 ) == 0;
}

/// The MCP allow-prefix table: the single authority for which catalog
/// families the projection exposes and tools/call accepts. Both
/// surfaceIdAllowed (the enforcement) and surfaceAllowedPrefixes (the
/// rendering, e.g. the tools/call denial message) read THIS table so the
/// rendered policy cannot drift from the enforced one.
QStringList allowedPrefixTable()
{
    return {
        QStringLiteral( "rs:" ),
        QStringLiteral( "gdal:" ),
        QStringLiteral( "gdal_tools:" ),
        QStringLiteral( "io:" ),     // geospatial I/O foundation: convert/inspect/doctor (Foundation 4.0)
        QStringLiteral( "otb:" ),
        QStringLiteral( "otb_tools:" ),
        QStringLiteral( "qgis:" ),
        QStringLiteral( "qgis_algorithms:" ),
        QStringLiteral( "opencv:" ), // operator surface uses opencv: filters
        QStringLiteral( "view:" ),   // agent interaction view tools
        QStringLiteral( "roi:" ),    // agent interaction roi tools
        QStringLiteral( "canvas:" ), // agent interaction canvas tools
        QStringLiteral( "layer:" ),  // agent interaction layer tools
        QStringLiteral( "raster:" ), // agent raster display tools
        QStringLiteral( "data:" ),   // data manager tools
        QStringLiteral( "spatial:" ), // spatial inspection/catalog tools (ADR 0122)
        QStringLiteral( "layout:" ),  // cartographic layout tools (Layout Studio)
        QStringLiteral( "temporal:" ), // temporal collection discovery/preflight tools
        QStringLiteral( "cartography:" ), // MapSpec compose/preflight/repair, components, charts (ADR 0127/0128)
        QStringLiteral( "style:" ),       // declarative StyleSpec list/describe/apply (cartography module)
        QStringLiteral( "template:" ),    // map template search/describe/validate/preview (cartography module)
        QStringLiteral( "solution:" ),    // Platform 5.0 solution knowledge: search/describe/validate/instantiate
        QStringLiteral( "symbology:" ),   // structured symbology apply/rollback (ADR 0128)
        QStringLiteral( "workflow:" ),    // static workflow preflight (ADR 0128)
        QStringLiteral( "workbench:" ),   // workbench context projection (read-only, 10.0)
        QStringLiteral( "workspace:" ),   // workspace command/undo tools (ADR 0128)
        QStringLiteral( "project:" ),     // workspace governance summary/search/health (Platform 3.0)
        QStringLiteral( "asset:" ),       // governed asset inspect/validate/relink (Platform 3.0)
        QStringLiteral( "collection:" ),  // governed datasets + smart collections (Platform 3.0)
        QStringLiteral( "lineage:" ),     // transitive lineage queries (Platform 3.0)
        QStringLiteral( "result:" ),      // governed result records (Platform 3.0)
        QStringLiteral( "run:" ),         // workflow run comparison (Platform 3.0)
        QStringLiteral( "harness:" ),     // Harness 4.0: taxonomy/manifest/preflight/plan/verify/recipe
        QStringLiteral( "mission:" ),     // mission context/timeline/advance (Workbench 12.0)
        QStringLiteral( "explain:" ),     // why-this-step explanations (RS14-15, exp.step_explanation.v1)
    };
}

} // namespace

std::vector<SurfaceTool> collectSurfaceTools( const SurfaceQuery &query )
{
    std::vector<SurfaceTool> tools;

    for ( const meta_protocol::MetaToolDef &def : meta_protocol::defs() )
    {
        SurfaceTool tool;
        tool.name = def.name;
        tool.description = def.description;
        tool.family = "meta";
        tool.source = SurfaceToolSource::MetaProtocol;
        tool.inputSchema = meta_protocol::schema( def );
        tools.push_back( std::move( tool ) );
    }

    for ( const auto &def : sicnu::agent::dataPlatformToolDefs() )
    {
        SurfaceTool tool;
        tool.name = def.name;
        tool.description = def.description;
        tool.family = surfaceToolFamily( def.name );
        tool.source = SurfaceToolSource::DataPlatform;
        Json::Value schema = Json::Value( Json::objectValue );
        schema["type"] = "object";
        Json::Value properties = Json::Value( Json::objectValue );
        Json::Value required = Json::Value( Json::arrayValue );
        for ( const auto &input : def.inputs )
        {
            Json::Value prop = Json::Value( Json::objectValue );
            prop["type"] = input.type;
            prop["description"] = input.description;
            properties[input.name] = prop;
            if ( input.required )
                required.append( input.name );
        }
        schema["properties"] = properties;
        if ( !required.empty() )
            schema["required"] = required;
        tool.inputSchema = std::move( schema );
        tools.push_back( std::move( tool ) );
    }

    // NOTE: the catalog singleton self-initializes in its constructor —
    // deliberately NOT re-initialized here: initializeDefaults() wipes
    // runtime custom tools, and the projection must be a read-only view.

    for ( const auto &catalogTool : AgentToolCatalog::instance().listTools() )
    {
        const QString id = QString::fromStdString( catalogTool.name );
        if ( !surfaceIdAllowed( id, nullptr ) )
            continue;
        if ( !query.includeGuiOnly && headlessHidesCatalogTool( catalogTool ) )
            continue;
        SurfaceTool tool;
        tool.name = catalogTool.name;
        tool.description = catalogTool.description;
        tool.family = surfaceToolFamily( catalogTool.name );
        tool.source = SurfaceToolSource::Catalog;
        tool.inputSchema = catalogTool.inputSchema;
        tools.push_back( std::move( tool ) );
    }
    return tools;
}

std::optional<SurfaceTool> findSurfaceTool( const std::string &name, const SurfaceQuery &query )
{
    for ( SurfaceTool &tool : collectSurfaceTools( query ) )
    {
        if ( tool.name == name )
            return tool;
    }
    return std::nullopt;
}

std::string surfaceToolFamily( const std::string &name )
{
    const auto colon = name.find( ':' );
    if ( colon == std::string::npos )
        return "meta";
    return name.substr( 0, colon );
}

std::vector<std::string> surfaceFamilies( const SurfaceQuery &query )
{
    std::vector<std::string> families;
    for ( const SurfaceTool &tool : collectSurfaceTools( query ) )
        families.push_back( tool.family );
    std::sort( families.begin(), families.end() );
    families.erase( std::unique( families.begin(), families.end() ), families.end() );
    return families;
}

bool surfaceIdAllowed( const QString &id, bool *isCustomTools )
{
    if ( isCustomTools )
        *isCustomTools = false;

    QString checkId = id;
    if ( checkId.startsWith( QStringLiteral( "processing:" ) ) )
    {
        checkId = checkId.mid( 11 );
    }

    static const QStringList kAllowed = allowedPrefixTable();
    for ( const QString &prefix : kAllowed )
    {
        if ( checkId.startsWith( prefix ) )
            return true;
    }
    if ( id.startsWith( QStringLiteral( "custom_tools:" ) ) )
    {
        if ( isCustomTools )
            *isCustomTools = true;
        return false;
    }
    return false;
}

QStringList surfaceAllowedPrefixes()
{
    return allowedPrefixTable();
}

bool headlessHidesCatalogTool( const AgentTool &tool )
{
    if ( !headlessHidesGuiTools() )
        return false;
    if ( tool.category != ToolCategory::Interaction )
        return false;
    if ( !guiOnlyCatalogEntry( tool ) )
        return false;
    return !sicnu::agent::InteractionToolRegistry::instance().hasTool( tool.name );
}

bool surfacePathOutsideWorkspace( const QString &pathValue, const QString &workspaceRoot,
                                  QString *detail )
{
    // An unset/blank root means NO sandbox is configured: nothing is outside
    // it. (Without this guard an empty root would canonicalize to the CWD and
    // silently sandbox against the process working directory instead.)
    if ( workspaceRoot.trimmed().isEmpty() )
        return false;
    if ( pathValue.isEmpty() )
        return false;

    // URL / VSI virtual-path awareness (#722-era remote policy): a network
    // data reference is NOT a filesystem path — treating one as relative
    // (QFileInfo::isAbsolute() == false for "https://host/x.tif") wrongly
    // ALLOWED any URL, while on Linux "/vsicurl/https://..." canonicalized
    // outside the workspace and was wrongly REJECTED. Policy: remote
    // http(s) data references (optionally /vsicurl/-prefixed) are allowed
    // read-only data inputs (bounded by GDAL HTTP timeouts); file:// maps to
    // its local path and falls through to the workspace check; every other
    // scheme is rejected. SICNU_MCP_ALLOW_REMOTE=0 restores strict local-only.
    {
        const QString trimmed = pathValue.trimmed();
        const QString lowered = trimmed.toLower();
        const bool vsiPrefixed = lowered.startsWith( QStringLiteral( "/vsicurl/" ) );
        const bool vsiOther = lowered.startsWith( QStringLiteral( "/vsi" ) ) && !vsiPrefixed;
        const bool httpUrl = lowered.startsWith( QStringLiteral( "http://" ) ) ||
                             lowered.startsWith( QStringLiteral( "https://" ) );
        const bool fileUrl = lowered.startsWith( QStringLiteral( "file://" ) );
        if ( vsiOther && !vsiPrefixed )
        {
            if ( detail )
                *detail = QStringLiteral( "Only /vsicurl/ remote sources are supported: %1" ).arg( pathValue );
            return true;
        }
        if ( httpUrl || vsiPrefixed )
        {
            if ( !envFlagEnabled( "SICNU_MCP_ALLOW_REMOTE" ) )
            {
                if ( detail )
                    *detail = QStringLiteral( "Remote data references are disabled "
                                              "(set SICNU_MCP_ALLOW_REMOTE=1): %1" ).arg( pathValue );
                return true;
            }
            return false; // scheme-validated remote reference, not a workspace path
        }
        if ( fileUrl )
        {
            const QUrl url( trimmed );
            // file:///abs/path -> local path; falls through to the workspace check.
            QString local = url.toLocalFile();
            if ( local.isEmpty() )
                local = trimmed.mid( 7 );
            if ( !surfacePathOutsideWorkspace( local, workspaceRoot, detail ) )
                return false;
            if ( detail && detail->isEmpty() )
                *detail = QStringLiteral( "Path outside SICNU_MCP_WORKSPACE: %1" ).arg( pathValue );
            return true;
        }
    }

    QString path = pathValue;
    if ( path.startsWith( QLatin1Char( '~' ) ) )
    {
        path = QDir::homePath() + path.mid( 1 );
    }

    QString workspaceCanon = QDir( workspaceRoot ).canonicalPath();
    if ( workspaceCanon.isEmpty() )
        workspaceCanon = QFileInfo( workspaceRoot ).absoluteFilePath();
    if ( workspaceCanon.isEmpty() )
        return false;

    const QFileInfo fi( path );
    QString resolved;
    if ( fi.isAbsolute() )
    {
        if ( fi.exists() )
        {
            resolved = fi.canonicalFilePath();
        }
        else
        {
            // Non-existent output path: resolve parent dir + filename
            QDir parent = fi.dir();
            QString parentCanon = parent.canonicalPath();
            if ( parentCanon.isEmpty() )
                parentCanon = parent.absolutePath();
            resolved = QDir( parentCanon ).filePath( fi.fileName() );
        }
    }
    else
    {
        const QString joined = QDir( workspaceCanon ).filePath( path );
        const QFileInfo fiJoined( joined );
        if ( fiJoined.exists() )
        {
            resolved = fiJoined.canonicalFilePath();
        }
        else
        {
            QDir parent = fiJoined.dir();
            QString parentCanon = parent.canonicalPath();
            if ( parentCanon.isEmpty() )
                parentCanon = parent.absolutePath();
            resolved = QDir( parentCanon ).filePath( fiJoined.fileName() );
        }
    }

    const QString normResolved = QDir::cleanPath( resolved );
    const QString normWorkspace = QDir::cleanPath( workspaceCanon );

    if ( normResolved == normWorkspace )
        return false;
    if ( normResolved.startsWith( normWorkspace + QLatin1Char( '/' ) ) )
        return false;

    if ( detail )
    {
        *detail = QStringLiteral( "Path outside SICNU_MCP_WORKSPACE: %1" ).arg( pathValue );
    }
    return true;
}

} // namespace sicnu::agent::tool_catalog
