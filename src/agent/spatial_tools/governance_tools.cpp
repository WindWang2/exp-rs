// governance_tools.cpp — see governance_tools.h for the contract.
#include "governance_tools.h"

#include "spatial_tool.h"

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"
#include "data/governance/relink_service.h"
#include "data/governance/workspace_service.h"
#include "data/governance/workspace_validator.h"
#include "workspace_state.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <json/json.h>
#include <json/reader.h>

#include <algorithm>
#include <sstream>
#include <string>

namespace sicnu::agent::spatial_tools
{

using sicnu::agent::AgentServices;
using sicnu::workspace::GovernedAsset;
using sicnu::workspace::WorkspacePage;
using sicnu::workspace::WorkspaceQuery;
using sicnu::workspace::WorkspaceService;

namespace
{

constexpr int kMaxLimit = 100;

WorkspaceService *service()
{
    return AgentServices::instance().workspaceService();
}

SpatialToolResult unavailable()
{
    return SpatialToolResult::failure( "workspace governance service is unavailable",
                                       "WORKSPACE_UNAVAILABLE", "runtime", false );
}

int clampLimit( const Json::Value &input )
{
    const Json::Value limit = input.get( "limit", Json::Value( 50 ) );
    const int raw = limit.isNumeric() ? limit.asInt() : 50;
    return std::clamp( raw, 1, kMaxLimit );
}

qint64 clampOffset( const Json::Value &input )
{
    const Json::Value offset = input.get( "offset", Json::Value( 0 ) );
    return offset.isNumeric() ? std::max<qint64>( 0, offset.asInt64() ) : 0;
}

Json::Value jsonFromQt( const QJsonObject &object )
{
    const QByteArray raw = QJsonDocument( object ).toJson( QJsonDocument::Compact );
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream( raw.toStdString() );
    if ( Json::parseFromStream( builder, stream, &parsed, &errors ) )
        return parsed;
    return Json::Value( Json::objectValue );
}

Json::Value parseJsonText( const QJsonObject &object, const QString &key )
{
    const QJsonDocument doc = QJsonDocument::fromJson( object.value( key ).toString().toUtf8() );
    return doc.object().isEmpty() && doc.array().isEmpty()
               ? Json::Value( Json::objectValue )
               : Json::Value( doc.toJson( QJsonDocument::Compact ).toStdString() );
}

Json::Value rowToJson( const QVariantMap &row )
{
    Json::Value out( Json::objectValue );
    for ( auto it = row.constBegin(); it != row.constEnd(); ++it )
    {
        const QVariant value = it.value();
        if ( value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::Int )
            out[it.key().toStdString()] = Json::Value( ( Json::Int64 ) value.toLongLong() );
        else
            out[it.key().toStdString()] = Json::Value( value.toString().toStdString() );
    }
    return out;
}

Json::Value pageToJson( const WorkspacePage &page )
{
    Json::Value out( Json::objectValue );
    out["total"] = ( Json::Int64 ) page.total;
    out["offsetReturned"] = ( Json::Int64 ) page.items.size();
    Json::Value items( Json::arrayValue );
    for ( const QVariantMap &row : page.items )
        items.append( rowToJson( row ) );
    out["items"] = items;
    return out;
}

Json::Value schemaForObject( const std::vector<std::pair<std::string, std::string>> &properties,
                            const std::vector<std::string> &required )
{
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );
    for ( const auto &prop : properties )
    {
        Json::Value p( Json::objectValue );
        p["type"] = prop.second;
        props[prop.first] = p;
    }
    schema["properties"] = props;
    Json::Value req( Json::arrayValue );
    for ( const std::string &r : required )
        req.append( r );
    schema["required"] = req;
    return schema;
}

// ---------------------------------------------------------------------------
// Tool implementations (thin service adapters)
// ---------------------------------------------------------------------------

/// Small adapter so the tool file does not need the full store header types.
class GovernanceStoreAuditAdapter
{
  public:
    struct Entry
    {
        QString action;
        QString entityKind;
        QString entityId;
        qint64 tsMs = 0;
    };
    static QVector<Entry> tail( WorkspaceService &ws, int limit )
    {
        QVector<Entry> out;
        for ( const auto &e : ws.auditTail( limit ) )
            out.append( Entry{ e.action, e.entityKind, e.entityId, e.tsMs } );
        return out;
    }
};

class ProjectSummaryTool final : public SpatialTool
{
  public:
    std::string name() const override { return "project:summary"; }
    std::string displayName() const override { return "Project Summary"; }
    std::string description() const override
    {
        return "Bounded overview of the governed workspace: asset/dataset/result/run counts, "
               "asset kind and state facets, and the most recent audit entries. Use this first "
               "to orient before any search.";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "summary" }; }
    Json::Value inputSchema() const override
    {
        return schemaForObject( { { "auditLimit", "integer" } }, {} );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "counts", "object" }, { "facets", "object" } }, {} );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        if ( !ws || !ws->isStoreOpen() )
            return unavailable();

        Json::Value out( Json::objectValue );
        Json::Value counts( Json::objectValue );
        counts["assets"] = ( Json::Int64 ) ws->store().assetCount();
        counts["datasets"] = ( Json::Int64 ) ws->datasets().size();
        counts["results"] = ( Json::Int64 ) ws->results().size();
        counts["runs"] = ( Json::Int64 ) ws->runs().size();
        counts["experiments"] = ( Json::Int64 ) ws->experiments().size();
        counts["smartCollections"] = ( Json::Int64 ) ws->smartCollections().size();
        counts["orphanResults"] = ( Json::Int64 ) ws->orphanResults().size();
        out["counts"] = counts;

        Json::Value facets( Json::objectValue );
        for ( const QString &field : { QStringLiteral( "kind" ), QStringLiteral( "state" ), QStringLiteral( "sensor" ) } )
        {
            WorkspaceQuery query;
            query.set = sicnu::workspace::EntitySet::Assets;
            const WorkspacePage facet = ws->query( query, field );
            Json::Value arr( Json::arrayValue );
            for ( const auto &fc : facet.facets )
            {
                Json::Value entry( Json::objectValue );
                entry["value"] = fc.value.toStdString();
                entry["count"] = ( Json::Int64 ) fc.count;
                arr.append( entry );
            }
            facets[field.toStdString()] = arr;
        }
        out["facets"] = facets;

        const int auditLimit = std::clamp( input.get( "auditLimit", Json::Value( 10 ) ).asInt(), 1, 50 );
        Json::Value audit( Json::arrayValue );
        for ( const GovernanceStoreAuditAdapter::Entry &entry : GovernanceStoreAuditAdapter::tail( *ws, auditLimit ) )
        {
            Json::Value a( Json::objectValue );
            a["action"] = entry.action.toStdString();
            a["entityKind"] = entry.entityKind.toStdString();
            a["entityId"] = entry.entityId.toStdString();
            a["tsMs"] = ( Json::Int64 ) entry.tsMs;
            audit.append( a );
        }
        out["recentAudit"] = audit;
        return SpatialToolResult::ok( out );
    }
};

class ProjectSearchTool final : public SpatialTool
{
  public:
    std::string name() const override { return "project:search"; }
    std::string displayName() const override { return "Project Search"; }
    std::string description() const override
    {
        return "Paged, filtered search over governed workspace entities. Set=assets|results|runs|datasets. "
               "Filters: text, kind, state, sensor, modality, crs, tag, runId; acquiredFromMs/acquiredToMs for "
               "assets. Returns total + one page (offset/limit, limit<=100). Facet a field via facetField.";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "search" }; }
    Json::Value inputSchema() const override
    {
        return schemaForObject(
            { { "set", "string" }, { "text", "string" }, { "kind", "string" }, { "state", "string" },
              { "sensor", "string" }, { "modality", "string" }, { "crs", "string" }, { "tag", "string" },
              { "runId", "string" }, { "offset", "integer" }, { "limit", "integer" }, { "facetField", "string" },
              { "sortBy", "string" } },
            {} );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "total", "integer" }, { "items", "array" } }, {} );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        if ( !ws || !ws->isStoreOpen() )
            return unavailable();

        WorkspaceQuery query;
        const std::string set = input.get( "set", Json::Value( "assets" ) ).asString();
        if ( set == "results" ) query.set = sicnu::workspace::EntitySet::Results;
        else if ( set == "runs" ) query.set = sicnu::workspace::EntitySet::Runs;
        else if ( set == "datasets" ) query.set = sicnu::workspace::EntitySet::Datasets;
        else query.set = sicnu::workspace::EntitySet::Assets;

        for ( const char *field : { "text", "kind", "state", "sensor", "modality", "crs", "tag", "runId", "sortBy" } )
        {
            if ( input.isMember( field ) && input[field].isString() )
            {
                const QString value = QString::fromStdString( input[field].asString() );
                if ( !value.isEmpty() )
                {
                    if ( std::string( field ) == "text" ) query.text = value;
                    else if ( std::string( field ) == "kind" ) query.kind = value;
                    else if ( std::string( field ) == "state" ) query.state = value;
                    else if ( std::string( field ) == "sensor" ) query.sensor = value;
                    else if ( std::string( field ) == "modality" ) query.modality = value;
                    else if ( std::string( field ) == "crs" ) query.crs = value;
                    else if ( std::string( field ) == "tag" ) query.tag = value;
                    else if ( std::string( field ) == "runId" ) query.runId = value;
                    else query.sortBy = value;
                }
            }
        }
        query.offset = clampOffset( input );
        query.limit = clampLimit( input );

        QString facetField;
        if ( input.isMember( "facetField" ) && input["facetField"].isString() )
            facetField = QString::fromStdString( input["facetField"].asString() );

        return SpatialToolResult::ok( pageToJson( ws->query( query, facetField ) ) );
    }
};

class ProjectHealthTool final : public SpatialTool
{
  public:
    std::string name() const override { return "project:health"; }
    std::string displayName() const override { return "Project Health"; }
    std::string description() const override
    {
        return "Validates the whole workspace (missing files, changed content, broken references, orphan "
               "results) and returns a bounded machine-readable findings list with repair suggestions. "
               "Set fingerprints=true to recompute content digests (slower).";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "health" }; }
    Json::Value inputSchema() const override
    {
        return schemaForObject( { { "fingerprints", "boolean" }, { "maxDiagnostics", "integer" } }, {} );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "errors", "integer" }, { "warnings", "integer" } }, {} );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        if ( !ws || !ws->isStoreOpen() )
            return unavailable();
        sicnu::data::DataManager *manager = AgentServices::instance().dataManager();
        if ( !manager )
            return SpatialToolResult::failure( "data manager is unavailable", "WORKSPACE_UNAVAILABLE",
                                               "runtime", false );

        sicnu::workspace::ValidationOptions options;
        options.verifyFingerprints = input.get( "fingerprints", Json::Value( false ) ).asBool();
        options.maxDiagnostics =
            std::clamp( input.get( "maxDiagnostics", Json::Value( 200 ) ).asInt(), 1, 1000 );
        const sicnu::workspace::WorkspaceValidator validator( *manager, *ws );
        const sicnu::workspace::ValidationReport report = validator.validateProject( options );

        Json::Value out = jsonFromQt( report.toJson() );
        out["ok"] = report.ok();
        return SpatialToolResult::ok( out );
    }
};

class AssetInspectTool final : public SpatialTool
{
  public:
    std::string name() const override { return "asset:inspect"; }
    std::string displayName() const override { return "Asset Inspect"; }
    std::string description() const override
    {
        return "Full governed record for one asset: locator, kind/state, enrichment (fingerprint, sensor, "
               "modality, CRS, bands), tags, lineage summary and dependent results.";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "asset" }; }
    Json::Value inputSchema() const override
    {
        return schemaForObject( { { "assetId", "string" } }, { "assetId" } );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "assetId", "string" } }, { "assetId" } );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        if ( !ws || !ws->isStoreOpen() )
            return unavailable();
        std::string error;
        const QString assetId = requireStringField( input, "assetId", &error );
        if ( !error.empty() )
            return SpatialToolResult::failure( error, "INVALID_PARAMETER", "validation", false );

        const std::optional<GovernedAsset> row = ws->store().assetById( assetId );
        if ( !row )
            return SpatialToolResult::failure( "unknown asset " + assetId.toStdString(), "NOT_FOUND",
                                               "validation", false );
        Json::Value out( Json::objectValue );
        out["assetId"] = assetId.toStdString();
        out["displayName"] = row->displayName.toStdString();
        out["locator"] = row->canonicalSource.toStdString();
        out["kind"] = row->kind.toStdString();
        out["state"] = row->state.toStdString();
        out["persistence"] = row->persistence.toStdString();
        out["revision"] = ( Json::Int64 ) row->revision;
        out["fingerprint"] = row->contentFingerprint.toStdString();
        out["sizeBytes"] = ( Json::Int64 ) row->sizeBytes;
        out["sensor"] = row->sensor.toStdString();
        out["modality"] = row->modality.toStdString();
        out["crsWkt"] = row->crs.toStdString();
        out["bandCount"] = ( Json::Int64 ) row->bandCount;
        out["availability"] = row->availability.toStdString();
        Json::Value tags( Json::arrayValue );
        for ( const QString &tag : row->tags )
            tags.append( tag.toStdString() );
        out["tags"] = tags;

        Json::Value upstream( Json::arrayValue );
        for ( const QVariantMap &u : ws->lineageUpstream( assetId, 1 ) )
            upstream.append( rowToJson( u ) );
        out["immediateInputs"] = upstream;
        Json::Value results( Json::arrayValue );
        for ( const sicnu::workspace::ResultRecord &result : ws->resultsDependingOnAsset( assetId ) )
            results.append( result.id.toString().toStdString() );
        out["dependentResults"] = results;
        return SpatialToolResult::ok( out );
    }
};

class AssetValidateTool final : public SpatialTool
{
  public:
    std::string name() const override { return "asset:validate"; }
    std::string displayName() const override { return "Asset Validate"; }
    std::string description() const override
    {
        return "Integrity validation for one asset (existence, size/mtime drift, optional digest, CRS, "
               "band-count stability). Returns bounded machine-readable diagnostics.";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "asset" }; }
    Json::Value inputSchema() const override
    {
        return schemaForObject( { { "assetId", "string" }, { "fingerprints", "boolean" } },
                                             { "assetId" } );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "ok", "boolean" } }, { "ok" } );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        sicnu::data::DataManager *manager = AgentServices::instance().dataManager();
        if ( !ws || !ws->isStoreOpen() || !manager )
            return unavailable();
        std::string error;
        const QString assetId = requireStringField( input, "assetId", &error );
        if ( !error.empty() )
            return SpatialToolResult::failure( error, "INVALID_PARAMETER", "validation", false );

        sicnu::workspace::ValidationOptions options;
        options.verifyFingerprints = input.get( "fingerprints", Json::Value( false ) ).asBool();
        const sicnu::workspace::WorkspaceValidator validator( *manager, *ws );
        const sicnu::workspace::ValidationReport report = validator.validateAsset( assetId, options );
        Json::Value out = jsonFromQt( report.toJson() );
        out["ok"] = report.ok();
        return SpatialToolResult::ok( out );
    }
};

class AssetRelinkTool final : public SpatialTool
{
  public:
    std::string name() const override { return "asset:relink"; }
    std::string displayName() const override { return "Asset Relink"; }
    std::string description() const override
    {
        return "Relink one asset to a new locator (identity is preserved; structure compatibility is "
               "enforced by the data manager). Optionally verify the target content fingerprint first.";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "asset" }; }
    Json::Value inputSchema() const override
    {
        return schemaForObject(
            { { "assetId", "string" }, { "newPath", "string" }, { "verifyFingerprint", "boolean" } },
            { "assetId", "newPath" } );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "relinked", "boolean" } }, { "relinked" } );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        sicnu::data::DataManager *manager = AgentServices::instance().dataManager();
        if ( !ws || !ws->isStoreOpen() || !manager )
            return unavailable();
        std::string error;
        const QString assetId = requireStringField( input, "assetId", &error );
        if ( !error.empty() )
            return SpatialToolResult::failure( error, "INVALID_PARAMETER", "validation", false );
        const QString newPath = requireStringField( input, "newPath", &error );
        if ( !error.empty() )
            return SpatialToolResult::failure( error, "INVALID_PARAMETER", "validation", false );

        sicnu::workspace::RelinkService relink( *manager, *ws );
        QVector<sicnu::data::Diagnostic> diagnostics;
        const bool relinked = relink.relinkAsset( assetId, newPath, &diagnostics );
        Json::Value out( Json::objectValue );
        out["relinked"] = relinked;
        Json::Value diags( Json::arrayValue );
        for ( const sicnu::data::Diagnostic &d : diagnostics )
        {
            Json::Value dj( Json::objectValue );
            dj["code"] = d.code.toStdString();
            dj["message"] = d.message.toStdString();
            diags.append( dj );
        }
        out["diagnostics"] = diags;
        if ( !relinked )
            return SpatialToolResult::failure( "relink rejected; see diagnostics", "RELINK_REJECTED",
                                               "runtime", true );
        return SpatialToolResult::ok( out );
    }
};

class CollectionQueryTool final : public SpatialTool
{
  public:
    std::string name() const override { return "collection:query"; }
    std::string displayName() const override { return "Collection Query"; }
    std::string description() const override
    {
        return "Lists governed datasets and smart collections, or evaluates one smart collection "
               "(dynamic predicate membership; paged).";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "collection" }; }
    Json::Value inputSchema() const override
    {
        return schemaForObject( { { "smartCollectionId", "string" }, { "offset", "integer" },
                                               { "limit", "integer" } }, {} );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "datasets", "array" }, { "smartCollections", "array" } }, {} );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        if ( !ws || !ws->isStoreOpen() )
            return unavailable();

        Json::Value out( Json::objectValue );
        if ( input.isMember( "smartCollectionId" ) && input["smartCollectionId"].isString() )
        {
            const WorkspacePage page = ws->evaluateSmartCollection(
                QString::fromStdString( input["smartCollectionId"].asString() ), clampOffset( input ),
                clampLimit( input ) );
            out["members"] = pageToJson( page );
            return SpatialToolResult::ok( out );
        }

        Json::Value datasets( Json::arrayValue );
        for ( const sicnu::workspace::DatasetRecord &dataset : ws->datasets() )
        {
            Json::Value d( Json::objectValue );
            d["id"] = dataset.id.toString().toStdString();
            d["name"] = dataset.header.name.toStdString();
            d["kind"] = sicnu::workspace::datasetKindToString( dataset.kind ).toStdString();
            d["memberCount"] = ( Json::Int64 ) dataset.memberAssetIds.size();
            datasets.append( d );
        }
        out["datasets"] = datasets;
        Json::Value smart( Json::arrayValue );
        for ( const sicnu::workspace::SmartCollectionRecord &collection : ws->smartCollections() )
        {
            Json::Value c( Json::objectValue );
            c["id"] = collection.id.toString().toStdString();
            c["name"] = collection.header.name.toStdString();
            c["predicates"] = ( Json::Int64 ) collection.predicates.size();
            smart.append( c );
        }
        out["smartCollections"] = smart;
        return SpatialToolResult::ok( out );
    }
};

class LineageToolBase : public SpatialTool
{
  public:
    Json::Value inputSchema() const override
    {
        return schemaForObject( { { "assetId", "string" }, { "maxDepth", "integer" } },
                                             { "assetId" } );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "nodes", "array" } }, { "nodes" } );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        if ( !ws || !ws->isStoreOpen() )
            return unavailable();
        std::string error;
        const QString assetId = requireStringField( input, "assetId", &error );
        if ( !error.empty() )
            return SpatialToolResult::failure( error, "INVALID_PARAMETER", "validation", false );
        const int maxDepth = std::clamp( input.get( "maxDepth", Json::Value( 25 ) ).asInt(), 1, 64 );

        const QVector<QVariantMap> nodes = direction() == Direction::Upstream
                                               ? ws->lineageUpstream( assetId, maxDepth )
                                               : ws->lineageDownstream( assetId, maxDepth );
        Json::Value out( Json::objectValue );
        out["assetId"] = assetId.toStdString();
        Json::Value arr( Json::arrayValue );
        for ( const QVariantMap &node : nodes )
            arr.append( rowToJson( node ) );
        out["nodes"] = arr;
        out["count"] = ( Json::Int64 ) nodes.size();
        return SpatialToolResult::ok( out );
    }

  protected:
    enum class Direction
    {
        Upstream,
        Downstream
    };
    virtual Direction direction() const = 0;
};

class LineageUpstreamTool final : public LineageToolBase
{
  public:
    std::string name() const override { return "lineage:upstream"; }
    std::string displayName() const override { return "Lineage Upstream"; }
    std::string description() const override
    {
        return "Transitive inputs of an asset (where did this come from?). Returns depth-ordered nodes; "
               "cycle-safe, depth-bounded.";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "lineage" }; }

  protected:
    Direction direction() const override { return Direction::Upstream; }
};

class LineageDownstreamTool final : public LineageToolBase
{
  public:
    std::string name() const override { return "lineage:downstream"; }
    std::string displayName() const override { return "Lineage Downstream"; }
    std::string description() const override
    {
        return "Impact analysis seeds: transitive dependents of an asset (what is affected if this "
               "changes?). Cycle-safe, depth-bounded.";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "lineage" }; }

  protected:
    Direction direction() const override { return Direction::Downstream; }
};

class ResultInspectTool final : public SpatialTool
{
  public:
    std::string name() const override { return "result:inspect"; }
    std::string displayName() const override { return "Result Inspect"; }
    std::string description() const override
    {
        return "Full governed record for one result: semantic type, lifecycle status, producer "
               "(operator/run), inputs with revisions, artifacts with digests, metrics and quality.";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "result" }; }
    Json::Value inputSchema() const override
    {
        return schemaForObject( { { "resultId", "string" } }, { "resultId" } );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "resultId", "string" } }, { "resultId" } );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        if ( !ws || !ws->isStoreOpen() )
            return unavailable();
        std::string error;
        const QString resultId = requireStringField( input, "resultId", &error );
        if ( !error.empty() )
            return SpatialToolResult::failure( error, "INVALID_PARAMETER", "validation", false );

        const std::optional<sicnu::workspace::ResultRecord> record = ws->result( resultId );
        if ( !record )
            return SpatialToolResult::failure( "unknown result " + resultId.toStdString(), "NOT_FOUND",
                                               "validation", false );
        Json::Value out( Json::objectValue );
        out["resultId"] = resultId.toStdString();
        out["name"] = record->header.name.toStdString();
        out["semanticType"] =
            sicnu::workspace::resultSemanticTypeToString( record->semanticType ).toStdString();
        out["status"] = sicnu::workspace::resultStatusToString( record->status ).toStdString();
        out["revision"] = ( Json::Int64 ) record->header.revision;
        out["producer"] = jsonFromQt( record->producer );
        out["metrics"] = jsonFromQt( record->metrics );
        out["quality"] = jsonFromQt( record->quality );
        out["validationNotes"] = record->validationNotes.toStdString();
        Json::Value inputs( Json::arrayValue );
        for ( const sicnu::workspace::ResultInput &in : record->inputs )
        {
            Json::Value i( Json::objectValue );
            i["assetId"] = in.assetId.toStdString();
            i["revision"] = ( Json::Int64 ) in.revision;
            i["role"] = in.role.toStdString();
            inputs.append( i );
        }
        out["inputs"] = inputs;
        Json::Value artifacts( Json::arrayValue );
        for ( const sicnu::workspace::ResultArtifact &artifact : record->artifacts )
        {
            Json::Value a( Json::objectValue );
            a["path"] = artifact.path.toStdString();
            a["role"] = artifact.role.toStdString();
            a["digest"] = artifact.contentDigest.toStdString();
            artifacts.append( a );
        }
        out["artifacts"] = artifacts;
        return SpatialToolResult::ok( out );
    }
};

class RunCompareTool final : public SpatialTool
{
  public:
    std::string name() const override { return "run:compare"; }
    std::string displayName() const override { return "Run Compare"; }
    std::string description() const override
    {
        return "Compares two workflow runs side by side: state, timing, workflow ids and output asset "
               "counts. Use result:inspect on each run's results for metric-level comparison.";
    }
    std::vector<std::string> tags() const override { return { "workspace", "governance", "run" }; }
    Json::Value inputSchema() const override
    {
        return schemaForObject( { { "runIdA", "string" }, { "runIdB", "string" } },
                                             { "runIdA", "runIdB" } );
    }
    Json::Value outputSchema() const override
    {
        return schemaForObject( { { "runA", "object" }, { "runB", "object" } }, {} );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
        WorkspaceService *ws = service();
        if ( !ws || !ws->isStoreOpen() )
            return unavailable();
        std::string error;
        const QString runIdA = requireStringField( input, "runIdA", &error );
        if ( !error.empty() )
            return SpatialToolResult::failure( error, "INVALID_PARAMETER", "validation", false );
        const QString runIdB = requireStringField( input, "runIdB", &error );
        if ( !error.empty() )
            return SpatialToolResult::failure( error, "INVALID_PARAMETER", "validation", false );

        const auto runA = ws->run( runIdA );
        const auto runB = ws->run( runIdB );
        if ( !runA )
            return SpatialToolResult::failure( "unknown run " + runIdA.toStdString(), "NOT_FOUND",
                                               "validation", false );
        if ( !runB )
            return SpatialToolResult::failure( "unknown run " + runIdB.toStdString(), "NOT_FOUND",
                                               "validation", false );

        auto describe = []( const sicnu::workspace::RunRecord &run ) {
            Json::Value r( Json::objectValue );
            r["runId"] = run.id.toStdString();
            r["workflowId"] = run.workflowId.toStdString();
            r["state"] = run.state.toStdString();
            r["startedMs"] = ( Json::Int64 ) run.startedMs;
            r["finishedMs"] = ( Json::Int64 ) run.finishedMs;
            r["durationMs"] = ( Json::Int64 ) qMax<qint64>( 0, run.finishedMs - run.startedMs );
            r["outputCount"] = ( Json::Int64 ) run.outputAssetIds.size();
            return r;
        };
        Json::Value out( Json::objectValue );
        out["runA"] = describe( *runA );
        out["runB"] = describe( *runB );
        return SpatialToolResult::ok( out );
    }
};

} // namespace

void registerGovernanceTools()
{
    static const std::vector<SpatialToolPtr> kTools = {
        std::make_shared<ProjectSummaryTool>(),
        std::make_shared<ProjectSearchTool>(),
        std::make_shared<ProjectHealthTool>(),
        std::make_shared<AssetInspectTool>(),
        std::make_shared<AssetValidateTool>(),
        std::make_shared<AssetRelinkTool>(),
        std::make_shared<CollectionQueryTool>(),
        std::make_shared<LineageUpstreamTool>(),
        std::make_shared<LineageDownstreamTool>(),
        std::make_shared<ResultInspectTool>(),
        std::make_shared<RunCompareTool>(),
    };
    for ( const SpatialToolPtr &tool : kTools )
        SpatialToolRegistry::instance().registerTool( tool );
}

} // namespace sicnu::agent::spatial_tools
