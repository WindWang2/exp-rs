// grader_adapters.cpp — pure JSON→JSON document-shape projection.
// See grader_adapters.h for the projection discipline and item inventory.
#include "grader/grader_adapters.h"

#include "grader/grader_error.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace sicnu::grader {

namespace {

constexpr int kMaxMetricDocumentDepth = 8;

bool fail( GraderError &error, GraderErrorCode code, std::string message, std::string path )
{
    error = makeError( code, std::move( message ), std::move( path ) );
    return false;
}

bool stringMember( const Json::Value &object, const char *key, std::string &out )
{
    if ( !object.isMember( key ) || !object[key].isString() )
        return false;
    out = object[key].asString();
    return true;
}

/// jsoncpp-safe bounded integer read (isInt() throws on overflow for
/// hostile magnitudes in some builds; go through Int64 explicitly).
bool boundedIntMember( const Json::Value &object, const char *key, int &out )
{
    if ( !object.isMember( key ) || object[key].isBool() )
        return false;
    const Json::Value &value = object[key];
    if ( value.isUInt64() ) {
        if ( value.asUInt64() > static_cast<Json::UInt64>( std::numeric_limits<int>::max() ) )
            return false;
        out = static_cast<int>( value.asUInt64() );
        return true;
    }
    if ( !value.isIntegral() || !value.isInt64() )
        return false;
    const Json::Int64 signedValue = value.asInt64();
    if ( signedValue < std::numeric_limits<int>::min() || signedValue > std::numeric_limits<int>::max() )
        return false;
    out = static_cast<int>( signedValue );
    return true;
}

/// Copies a recorded member verbatim when present (projection, never
/// reinterpretation).
void copyMemberIfPresent( const Json::Value &from, const char *key, Json::Value &into )
{
    if ( from.isMember( key ) )
        into[key] = from[key];
}

/// Copies a recorded STRING member only when present and non-empty (keeps
/// lean items for envelope noise like errorMessage = "").
void copyNonEmptyString( const Json::Value &from, const char *key, Json::Value &into )
{
    std::string value;
    if ( stringMember( from, key, value ) && !value.empty() )
        into[key] = value;
}

void appendEdgeFact( Json::Value &facts, const char *member, const std::string &nodeId )
{
    if ( !facts[member].isArray() )
        facts[member] = Json::Value{ Json::arrayValue };
    facts[member].append( nodeId );
}

/// Strips a "prefix:" id spelling down to the bare recorded key.
std::string stripIdPrefix( const std::string &id, const char *prefix )
{
    const std::string spelling = std::string( prefix ) + ":";
    if ( id.rfind( spelling, 0 ) == 0 )
        return id.substr( spelling.size() );
    return id;
}

// Shared numeric-leaf walk for metric documents: numbers become metric
// observations keyed by dotted path; objects/arrays recurse (capped);
// strings/bools/nulls are NOT numeric observations and stay unprojected.
bool projectMetricLeaves( const Json::Value &node, const std::string &prefix, int depth,
                          const std::string &source, const std::string &idPrefix,
                          std::vector<GradeEvidenceItem> &out, GraderError &error )
{
    if ( depth > kMaxMetricDocumentDepth )
        return fail( error, GraderErrorCode::SchemaShapeInvalid,
                     "metric document nesting exceeds the projection depth cap " +
                         std::to_string( kMaxMetricDocumentDepth ) + " at '" + prefix + "'",
                     prefix );

    if ( node.isObject() ) {
        for ( const auto &name : node.getMemberNames() ) {
            if ( name.empty() )
                return fail( error, GraderErrorCode::SchemaShapeInvalid,
                             "metric document carries an empty metric name under '" + prefix + "'",
                             prefix );
            const std::string key = prefix.empty() ? name : prefix + "." + name;
            if ( !projectMetricLeaves( node[name], key, depth + 1, source, idPrefix, out, error ) )
                return false;
        }
        return true;
    }
    if ( node.isArray() ) {
        for ( Json::ArrayIndex i = 0; i < node.size(); ++i ) {
            const std::string key = prefix + "." + std::to_string( i );
            if ( !projectMetricLeaves( node[i], key, depth + 1, source, idPrefix, out, error ) )
                return false;
        }
        return true;
    }
    if ( node.isDouble() || node.isIntegral() ) {
        const double value = node.asDouble();
        if ( !std::isfinite( value ) )
            return fail( error, GraderErrorCode::SchemaShapeInvalid,
                         "metric '" + prefix + "' carries a non-finite value", prefix );
        GradeEvidenceItem item;
        item.evidenceId = idPrefix + ":" + prefix;
        item.kind = EvidenceKind::Metric;
        item.key = prefix;
        item.hasValue = true;
        item.value = value;
        item.source = source;
        out.push_back( std::move( item ) );
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// d17_provenance
// ---------------------------------------------------------------------------

std::vector<GradeEvidenceItem> provenanceToEvidence( const Json::Value &doc, GraderError &error )
{
    std::vector<GradeEvidenceItem> items;
    if ( !doc.isObject() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid, "provenance document must be an object", {} ),
               items;

    std::string version;
    if ( !stringMember( doc, "kind", version ) || version != "d17_provenance" )
        return fail( error, GraderErrorCode::SchemaShapeInvalid,
                     "provenance envelope kind must be 'd17_provenance'", "kind" ),
               items;
    if ( !stringMember( doc, "version", version ) || version != "1.0" )
        return fail( error, GraderErrorCode::SchemaVersionUnsupported,
                     "unsupported provenance version: " + ( doc.isMember( "version" ) && doc["version"].isString()
                                                                ? doc["version"].asString()
                                                                : std::string { "(missing)" } ),
                     "version" ),
               items;
    if ( !doc.isMember( "nodes" ) || !doc["nodes"].isArray() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid, "provenance requires a nodes array", "nodes" ),
               items;

    // Walk 1: identify the run node (exactly one per record) and collect ids.
    const Json::Value *runNode = nullptr;
    for ( const auto &node : doc["nodes"] ) {
        if ( !node.isObject() )
            continue;
        std::string kind;
        if ( stringMember( node, "kind", kind ) && kind == "run" ) {
            if ( runNode )
                return fail( error, GraderErrorCode::SchemaShapeInvalid,
                             "provenance carries more than one run node", "nodes" ),
                       items;
            runNode = &node;
        }
    }
    if ( !runNode || !stringMember( *runNode, "id", version ) || version.empty() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid,
                     "provenance carries no run node id", "nodes" ),
               items;
    const std::string runNodeId = version;
    const std::string idNamespace = "prov:" + runNodeId + ":";

    // Walk 2: project nodes into items.
    std::set<std::string> nodeIds;
    for ( const auto &node : doc["nodes"] ) {
        if ( !node.isObject() )
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "provenance node must be an object",
                         "nodes" ),
                   items;
        std::string id;
        if ( !stringMember( node, "id", id ) || id.empty() )
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "provenance node id must be a non-empty string",
                         "nodes" ),
                   items;
        std::string kind;
        if ( !stringMember( node, "kind", kind ) )
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "provenance node kind must be a string",
                         "nodes." + id ),
                   items;
        if ( !nodeIds.insert( id ).second )
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "duplicate provenance node id: " + id,
                         "nodes." + id ),
                   items;
        const Json::Value attributes =
            node.isMember( "attributes" ) && node["attributes"].isObject() ? node["attributes"]
                                                                           : Json::Value { Json::objectValue };

        GradeEvidenceItem item;
        item.facts = Json::Value { Json::objectValue };
        if ( kind == "run" ) {
            item.evidenceId = "prov:" + id;
            item.kind = EvidenceKind::Custom;
            item.key = id;
            for ( const char *member : { "workflowId", "workflowName", "planSignature", "schemaVersion" } )
                copyMemberIfPresent( attributes, member, item.facts );
        }
        else if ( kind == "nodeExec" )
        {
            item.evidenceId = idNamespace + id;
            item.kind = EvidenceKind::Stage;
            std::string nodeKey;
            if ( stringMember( attributes, "nodeId", nodeKey ) && !nodeKey.empty() )
                item.key = nodeKey;
            else
                item.key = stripIdPrefix( id, "node" );
            stringMember( attributes, "state", item.state );
            for ( const char *member :
                  { "operatorId", "lineageSignature", "elapsedMs", "isCacheHit", "originNodeId", "errorMessage" } )
                copyMemberIfPresent( attributes, member, item.facts );
        }
        else if ( kind == "artifact" )
        {
            item.evidenceId = idNamespace + id;
            item.kind = EvidenceKind::ArtifactState;
            std::string artifactKey;
            if ( stringMember( attributes, "path", artifactKey ) && !artifactKey.empty() )
                item.key = artifactKey;
            else
                item.key = stripIdPrefix( id, "artifact" );
            // The node's existence in the recorded graph IS the record of
            // presence — spelled honestly, not invented.
            item.state = "present";
            for ( const char *member : { "path", "fingerprint", "sizeBytes" } )
                copyMemberIfPresent( attributes, member, item.facts );
        }
        else
        {
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "unknown provenance node kind: " + kind,
                         "nodes." + id + ".kind" ),
                   items;
        }
        items.push_back( std::move( item ) );
    }

    // Walk 3: fold resolvable edges into artifact edge facts.
    if ( doc.isMember( "edges" ) ) {
        if ( !doc["edges"].isArray() )
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "provenance edges must be an array", "edges" ),
                   items;
        for ( const auto &edge : doc["edges"] ) {
            if ( !edge.isObject() )
                return fail( error, GraderErrorCode::SchemaShapeInvalid, "provenance edge must be an object",
                             "edges" ),
                       items;
            std::string from;
            std::string to;
            std::string kind;
            stringMember( edge, "from", from );
            stringMember( edge, "to", to );
            stringMember( edge, "kind", kind );
            // A dangling endpoint projects NO edge fact (the relation cannot
            // be verified against the recorded nodes) — never a fabricated
            // producer.
            if ( from.empty() || to.empty() || nodeIds.count( from ) == 0 || nodeIds.count( to ) == 0 )
                continue;
            for ( GradeEvidenceItem &item : items ) {
                if ( item.kind != EvidenceKind::ArtifactState ||
                     item.evidenceId != idNamespace + to )
                    continue;
                if ( kind == "produced" )
                    appendEdgeFact( item.facts, "producedBy", from );
                else if ( kind == "consumed" )
                    appendEdgeFact( item.facts, "consumedBy", from );
                else if ( kind == "reusedFrom" )
                    appendEdgeFact( item.facts, "reusedBy", from );
            }
        }
    }

    return items;
}

// ---------------------------------------------------------------------------
// WorkflowRun checkpoint
// ---------------------------------------------------------------------------

std::vector<GradeEvidenceItem> checkpointToEvidence( const Json::Value &doc, GraderError &error )
{
    std::vector<GradeEvidenceItem> items;
    if ( !doc.isObject() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid, "checkpoint document must be an object", {} ),
               items;

    int version = 0;
    if ( !boundedIntMember( doc, "version", version ) )
        return fail( error, GraderErrorCode::SchemaShapeInvalid,
                     "checkpoint serialization version must be an integer", "version" ),
               items;
    if ( version != 1 && version != 2 )
        return fail( error, GraderErrorCode::SchemaVersionUnsupported,
                     "unsupported checkpoint serialization version: " + std::to_string( version ),
                     "version" ),
               items;

    std::string runId;
    if ( !stringMember( doc, "runId", runId ) || runId.empty() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid,
                     "checkpoint runId must be a non-empty string", "runId" ),
               items;

    GradeEvidenceItem run;
    run.evidenceId = "ckpt:" + runId + ":run";
    run.kind = EvidenceKind::Custom;
    run.key = runId;
    stringMember( doc, "state", run.state );
    run.facts = Json::Value { Json::objectValue };
    copyNonEmptyString( doc, "workflowId", run.facts );
    copyMemberIfPresent( doc, "progress", run.facts );
    copyNonEmptyString( doc, "errorMessage", run.facts );
    items.push_back( std::move( run ) );

    if ( !doc.isMember( "stepPlans" ) || !doc["stepPlans"].isArray() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid, "checkpoint stepPlans must be an array",
                     "stepPlans" ),
               items;

    std::set<std::string> stepIds;
    for ( const auto &step : doc["stepPlans"] ) {
        if ( !step.isObject() )
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "step plan must be an object",
                         "stepPlans" ),
                   items;
        std::string stepId;
        if ( !stringMember( step, "stepId", stepId ) || stepId.empty() )
            return fail( error, GraderErrorCode::SchemaShapeInvalid,
                         "step plan stepId must be a non-empty string", "stepPlans" ),
                   items;
        if ( !stepIds.insert( stepId ).second )
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "duplicate stepId: " + stepId,
                         "stepPlans" ),
                   items;

        GradeEvidenceItem item;
        item.evidenceId = "ckpt:" + runId + ":step:" + stepId;
        item.kind = EvidenceKind::Stage;
        item.key = stepId;
        stringMember( step, "status", item.state );
        item.facts = Json::Value { Json::objectValue };
        copyNonEmptyString( step, "operatorId", item.facts );
        copyMemberIfPresent( step, "cacheHit", item.facts );
        copyNonEmptyString( step, "fingerprint", item.facts );
        copyNonEmptyString( step, "outputDigest", item.facts );
        copyMemberIfPresent( step, "outputSizeBytes", item.facts );
        copyNonEmptyString( step, "errorMessage", item.facts );
        items.push_back( std::move( item ) );
    }
    return items;
}

// ---------------------------------------------------------------------------
// MetricRecord
// ---------------------------------------------------------------------------

std::vector<GradeEvidenceItem> metricRecordToEvidence( const Json::Value &doc, GraderError &error )
{
    std::vector<GradeEvidenceItem> items;
    if ( !doc.isObject() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid, "metric record must be an object", {} ), items;

    // Missing member = v1-before-versioning record (the producer's own
    // migration rule); a PRESENT foreign version is refused.
    if ( doc.isMember( "metrics_schema_version" ) ) {
        int schemaVersion = 0;
        if ( !boundedIntMember( doc, "metrics_schema_version", schemaVersion ) )
            return fail( error, GraderErrorCode::SchemaShapeInvalid,
                         "metrics_schema_version must be an integer", "metrics_schema_version" ),
                   items;
        if ( schemaVersion != 1 )
            return fail( error, GraderErrorCode::SchemaVersionUnsupported,
                         "unsupported metrics_schema_version: " + std::to_string( schemaVersion ),
                         "metrics_schema_version" ),
                   items;
    }

    std::string metricsHash;
    if ( !stringMember( doc, "metrics_hash", metricsHash ) || metricsHash.empty() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid,
                     "metric record metrics_hash must be a non-empty string", "metrics_hash" ),
               items;
    if ( !doc.isMember( "metrics" ) || !doc["metrics"].isObject() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid, "metric record metrics must be an object",
                     "metrics" ),
               items;

    const std::string source = "metric_record:" + metricsHash;
    if ( !projectMetricLeaves( doc["metrics"], {}, 0, source, "metric_record:" + metricsHash, items, error ) )
        return {};
    return items;
}

// ---------------------------------------------------------------------------
// EvidenceProjector summary
// ---------------------------------------------------------------------------

std::vector<GradeEvidenceItem> projectorSummaryToEvidence( const Json::Value &doc, GraderError &error )
{
    std::vector<GradeEvidenceItem> items;
    if ( !doc.isObject() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid, "projector summary must be an object", {} ),
               items;

    int schemaVersion = 0;
    if ( !boundedIntMember( doc, "schema_version", schemaVersion ) )
        return fail( error, GraderErrorCode::SchemaShapeInvalid, "schema_version must be an integer",
                     "schema_version" ),
               items;
    if ( schemaVersion != 1 )
        return fail( error, GraderErrorCode::SchemaVersionUnsupported,
                     "unsupported projector schema_version: " + std::to_string( schemaVersion ),
                     "schema_version" ),
               items;

    std::string runId;
    if ( !stringMember( doc, "run_id", runId ) || runId.empty() )
        return fail( error, GraderErrorCode::SchemaShapeInvalid, "run_id must be a non-empty string", "run_id" ),
               items;
    const std::string idNamespace = "proj:" + runId + ":";

    const auto pushCustom = [runId]( std::vector<GradeEvidenceItem> &out, const std::string &evidenceId,
                                     const std::string &key, const std::string &state, Json::Value facts ) {
        GradeEvidenceItem item;
        item.evidenceId = evidenceId;
        item.kind = EvidenceKind::Custom;
        item.key = key.empty() ? runId : key;
        item.state = state;
        item.facts = std::move( facts );
        item.source = "projector_summary";
        out.push_back( std::move( item ) );
    };

    // Run status.
    std::string status;
    if ( stringMember( doc, "status", status ) && !status.empty() )
        pushCustom( items, idNamespace + "status", runId, status, Json::Value { Json::objectValue } );

    // Recorded dimension blocks: projected verbatim when present and
    // non-empty; an empty object is the projector's absence spelling and
    // projects NOTHING.
    for ( const char *dimension : { "identity", "environment", "steps", "completeness" } ) {
        if ( !doc.isMember( dimension ) )
            continue;
        if ( !doc[dimension].isObject() )
            return fail( error, GraderErrorCode::SchemaShapeInvalid,
                         std::string( dimension ) + " must be an object", dimension ),
                   items;
        if ( !doc[dimension].empty() )
            pushCustom( items, idNamespace + dimension, dimension, {}, doc[dimension] );
    }

    // Artifacts.
    if ( doc.isMember( "artifacts" ) ) {
        if ( !doc["artifacts"].isArray() )
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "artifacts must be an array", "artifacts" ),
                   items;
        for ( const auto &artifact : doc["artifacts"] ) {
            if ( !artifact.isObject() )
                return fail( error, GraderErrorCode::SchemaShapeInvalid, "artifact entry must be an object",
                             "artifacts" ),
                       items;
            std::string path;
            if ( !stringMember( artifact, "path", path ) || path.empty() )
                return fail( error, GraderErrorCode::SchemaShapeInvalid,
                             "artifact entry path must be a non-empty string", "artifacts" ),
                       items;
            GradeEvidenceItem item;
            item.evidenceId = idNamespace + "artifact:" + path;
            item.kind = EvidenceKind::ArtifactState;
            item.key = path;
            item.state = "present";
            item.facts = Json::Value { Json::objectValue };
            item.source = "projector_summary";
            for ( const char *member : { "role", "digest", "size_bytes" } )
                copyMemberIfPresent( artifact, member, item.facts );
            items.push_back( std::move( item ) );
        }
    }

    // Metrics: only a real metrics block carries a document; numeric leaves
    // become metric observations. An empty object is the absence spelling.
    if ( doc.isMember( "metrics" ) ) {
        if ( !doc["metrics"].isObject() )
            return fail( error, GraderErrorCode::SchemaShapeInvalid, "metrics must be an object", "metrics" ),
                   items;
        const Json::Value &metrics = doc["metrics"];
        if ( metrics.isMember( "document" ) ) {
            if ( !metrics["document"].isObject() )
                return fail( error, GraderErrorCode::SchemaShapeInvalid, "metrics.document must be an object",
                             "metrics.document" ),
                       items;
            if ( !projectMetricLeaves( metrics["document"], {}, 0, "projector_summary",
                                       idNamespace + "metric", items, error ) )
                return {};
        }
    }

    return items;
}

} // namespace sicnu::grader
