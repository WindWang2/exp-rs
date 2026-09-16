/***************************************************************************
 * cartography_operators.cpp — cartography:* RSOperator adapters
 *
 * Behavior mirrors the agent tools one-to-one (conditions → composition →
 * compile → preflight; preflight → repair ledger; find-layout → governed
 * export). Divergences would break the "same engine, same semantics" claim
 * that lets a workflow node and an agent tool interleave freely.
 ***************************************************************************/
#include "cartography_operators.h"

#include "../layout_tools/layout_service.h"
#include "../mapspec/mapspec.h"
#include "../mapspec/mapspec_compiler.h"
#include "../mapspec/mapspec_conditions.h"
#include "composition.h"
#include "design_tokens.h"
#include "export.h"
#include "produce.h"
#include "quality.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <QApplication>

#include <qgsapplication.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>

#include <QString>

#include <algorithm>

namespace sicnu::agent::cartography {

namespace {

using sicnu::operators::ErrorCode;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using sicnu::operators::RSOperatorMemoryPolicy;

/// Layout-bound engines need a constructed QgsApplication (GUI app, the
/// --mcp GUI binary, or the headless CLI — all three initialize QGIS).
/// Worker processes do not; refusing there is the honest answer, never a
/// silent no-op.
void requireQgisHost()
{
    if ( !qobject_cast<QgsApplication *>( QCoreApplication::instance() ) )
        throw RSOperatorError( ErrorCode::NotInitialized,
                               "cartography compose/export need an initialized QGIS host; "
                               "this process runs without one (worker/headless worker). "
                               "Run the step in the GUI app or the CLI pipeline host." );
}

void requireMapSpec( const Json::Value &params )
{
    if ( !params.isMember( "mapspec" ) || !params["mapspec"].isObject() )
        throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                               "Missing required parameter: mapspec (object)" );
}

/// Shared pre-compile pass: conditions + composition resolve IN PLACE —
/// `resolveCompositionPass` (composition.h) is the single engine-level
/// implementation the agent tools run; see it for the resolution contract.
/// The export/compose compose-family output contract: an object whose
/// "output" key carries the delivered artifact so downstream workflow steps
/// can reference it through the placeholder grammar (same shape rs:*
/// operators use).
Json::Value withOutput( Json::Value root, const char *outputValue )
{
    root["output"] = outputValue;
    return root;
}

class ComposeOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "cartography:compose"; }
    std::string displayName() const override { return "Compose MapSpec Layout"; }
    std::string group() const override { return "cartography"; }
    std::string description() const override
    {
        return "Compiles a MapSpec document into a QGIS print layout (created/replaced under "
               "spec.layout_name), resolves anchors/constraints and returns the quality "
               "report. Follow with cartography:repair until quality passes, then "
               "cartography:export.";
    }
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::UnsupportedForLargeRaster;
    }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        Json::Value props( Json::objectValue );
        Json::Value mapspec( Json::objectValue );
        mapspec["type"] = "object";
        mapspec["description"] = "MapSpec document (kind: map_spec)";
        props["mapspec"] = mapspec;
        schema["properties"] = props;
        Json::Value required( Json::arrayValue );
        required.append( "mapspec" );
        schema["required"] = required;
        return schema;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
        requireQgisHost();
        requireMapSpec( params );
        context.throwIfCancelled();
        context.reportProgress( 0.1, "resolving conditions and composition" );

        Json::Value spec = params["mapspec"];
        const Json::Value composition = resolveCompositionPass( spec );

        context.reportProgress( 0.5, "compiling layout" );
        QString error;
        QgsPrintLayout *layout = mapspec::MapSpecCompiler::compile( spec, &error );
        const Json::Value report = preflightMapSpec( spec );

        Json::Value out( Json::objectValue );
        out["mapspec"] = spec;
        out["composition"] = composition;
        if ( !layout )
        {
            out["compiled"] = false;
            out["quality"] = report;
            throw RSOperatorError( ErrorCode::InvalidInputData, error.toStdString(), out );
        }
        context.reportProgress( 0.9, "preflight" );
        out["compiled"] = true;
        out["layout_name"] = spec["layout_name"].asString();
        out["quality"] = report;
        out["structural_digest"] = structuralDigest( spec );
        const std::string outputName =
            spec["layout_name"].isString() ? spec["layout_name"].asString() : std::string();
        return withOutput( std::move( out ), outputName.c_str() );
    }
};

class PreflightOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "cartography:preflight"; }
    std::string displayName() const override { return "Preflight MapSpec"; }
    std::string group() const override { return "cartography"; }
    std::string description() const override
    {
        return "Deterministic preflight of a MapSpec document (layout rules, text fit, "
               "overlaps, safe areas). Qt-free: runs headless. Returns the quality report.";
    }
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        Json::Value props( Json::objectValue );
        Json::Value mapspec( Json::objectValue );
        mapspec["type"] = "object";
        props["mapspec"] = mapspec;
        schema["properties"] = props;
        Json::Value required( Json::arrayValue );
        required.append( "mapspec" );
        schema["required"] = required;
        return schema;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
        requireMapSpec( params );
        context.throwIfCancelled();
        // Preflight evaluates the RESOLVED composition and echoes the resolved
        // document (agent-tool contract: the report's `mapspec` member).
        Json::Value spec = params["mapspec"];
        resolveCompositionPass( spec );
        Json::Value out = preflightMapSpec( spec );
        out["mapspec"] = spec;
        return out;
    }
};

class ValidateOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "cartography:validate"; }
    std::string displayName() const override { return "Validate MapSpec"; }
    std::string group() const override { return "cartography"; }
    std::string description() const override
    {
        return "Structural MapSpec validation without compiling: envelope, ids, geometry, "
               "references, collection rules. Empty problems list means valid.";
    }
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        Json::Value props( Json::objectValue );
        Json::Value mapspec( Json::objectValue );
        mapspec["type"] = "object";
        props["mapspec"] = mapspec;
        schema["properties"] = props;
        Json::Value required( Json::arrayValue );
        required.append( "mapspec" );
        schema["required"] = required;
        return schema;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
        requireMapSpec( params );
        context.throwIfCancelled();
        Json::Value out( Json::objectValue );
        Json::Value problems( Json::arrayValue );
        for ( const auto &problem : mapspec::validateMapSpec( params["mapspec"] ) )
            problems.append( problem );
        out["problems"] = problems;
        out["valid"] = problems.empty();
        return out;
    }
};

class RepairOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "cartography:repair"; }
    std::string displayName() const override { return "Repair MapSpec"; }
    std::string group() const override { return "cartography"; }
    std::string description() const override
    {
        return "Applies the bounded repair pass to a MapSpec against a preflight report and "
               "returns the repaired document with an applied/still_reported ledger. "
               "Qt-free: runs headless.";
    }
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override
    {
        // Input contract matches the agent RepairTool verbatim (D10-3).
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        Json::Value props( Json::objectValue );
        Json::Value mapspec( Json::objectValue );
        mapspec["type"] = "object";
        props["mapspec"] = mapspec;
        Json::Value iterations( Json::objectValue );
        iterations["type"] = "integer";
        props["max_iterations"] = iterations;
        schema["properties"] = props;
        Json::Value required( Json::arrayValue );
        required.append( "mapspec" );
        schema["required"] = required;
        return schema;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
        requireMapSpec( params );
        context.throwIfCancelled();
        // Tool-identical loop: solve once before repairing, then repair +
        // re-preflight up to max_iterations (default 3, clamped 1..10) with a
        // per-pass ledger; stops when a pass applies nothing.
        int maxIterations = params.isMember( "max_iterations" ) && params["max_iterations"].isInt()
                                ? std::clamp( params["max_iterations"].asInt(), 1, 10 )
                                : 3;
        Json::Value spec = params["mapspec"];
        resolveCompositionPass( spec );
        int totalRepairs = 0;
        int iterations = 0;
        Json::Value quality = preflightMapSpec( spec );
        Json::Value repairLedger( Json::arrayValue );
        while ( iterations < maxIterations && !quality["passed"].asBool() )
        {
            Json::Value passLedger;
            const int repairs = repairMapSpecWithLedger( spec, quality, &passLedger );
            if ( repairs == 0 )
                break;
            totalRepairs += repairs;
            for ( const auto &entry : passLedger )
            {
                Json::Value record = entry;
                record["pass"] = iterations + 1;
                repairLedger.append( record );
            }
            ++iterations;
            context.throwIfCancelled();
            quality = preflightMapSpec( spec );
        }
        Json::Value out( Json::objectValue );
        out["mapspec"] = spec;
        out["repairs_applied"] = totalRepairs;
        out["iterations"] = iterations;
        out["repair_ledger"] = repairLedger;
        out["quality"] = quality;
        return out;
    }
};

class ExportOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "cartography:export"; }
    std::string displayName() const override { return "Export Composed Map"; }
    std::string group() const override { return "cartography"; }
    std::string description() const override
    {
        return "Governed atomic export of a composed layout (png|pdf|svg, dpi 72..1200). "
               "png supports page selection; pdf/svg always export all pages and refuse a "
               "declared page selection. Writes temp → verifies → sha256 → renames. Result "
               "\"output\" is the delivered file path (workflow-chainable).";
    }
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::UnsupportedForLargeRaster;
    }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        Json::Value props( Json::objectValue );
        props["layout"]["type"] = "string";
        Json::Value formats( Json::arrayValue );
        formats.append( "png" );
        formats.append( "pdf" );
        formats.append( "svg" );
        props["format"]["type"] = "string";
        props["format"]["enum"] = formats;
        props["directory"]["type"] = "string";
        props["file_name"]["type"] = "string";
        props["dpi"]["type"] = "number";
        Json::Value pages( Json::objectValue );
        pages["type"] = "array";
        Json::Value pageIndex( Json::objectValue );
        pageIndex["type"] = "integer";
        pages["items"] = pageIndex;
        props["pages"] = pages;
        schema["properties"] = props;
        Json::Value required( Json::arrayValue );
        required.append( "layout" );
        required.append( "format" );
        required.append( "directory" );
        schema["required"] = required;
        return schema;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
        requireQgisHost();
        context.throwIfCancelled();

        const auto requireString = [&]( const char *key ) -> std::string {
            if ( !params.isMember( key ) || !params[key].isString() ||
                 params[key].asString().empty() )
                throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                                       std::string( "Missing required parameter: " ) + key +
                                           " (non-empty string)" );
            return params[key].asString();
        };
        const std::string layoutName = requireString( "layout" );
        const std::string format = requireString( "format" );
        const std::string directory = requireString( "directory" );

        context.reportProgress( 0.2, "locating layout" );
        QgsPrintLayout *layout =
            sicnu::agent::layout_tools::LayoutService::instance().findLayout(
                QString::fromStdString( layoutName ) );
        if ( !layout )
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "No layout named '" + layoutName +
                    "'; compose the MapSpec first (cartography:compose)" );

        MapExportRequest request;
        request.format = format;
        request.directory = directory;
        if ( params.isMember( "file_name" ) && params["file_name"].isString() )
            request.file_name = params["file_name"].asString();
        if ( params.isMember( "dpi" ) && params["dpi"].isNumeric() )
            request.dpi = params["dpi"].asDouble();
        if ( params.isMember( "pages" ) && params["pages"].isArray() )
            for ( const auto &page : params["pages"] )
                if ( page.isIntegral() )
                    request.pages.push_back( page.asInt() );

        context.reportProgress( 0.6, "exporting" );
        const MapExportResult result = exportMapLayout( layout, request );
        if ( !result.ok )
            throw RSOperatorError( ErrorCode::QgisProcessingError,
                                   result.error.toStdString() );

        context.reportProgress( 0.95, "finalizing" );
        Json::Value out = mapExportResultToJson( result );
        out["format"] = format;
        out["dpi"] = request.dpi;
        if ( !request.pages.empty() )
        {
            Json::Value pagesJson( Json::arrayValue );
            for ( const int page : request.pages )
                pagesJson.append( page );
            out["pages"] = pagesJson;
        }
        const std::string outputPath = out["path"].asString();
        return withOutput( std::move( out ), outputPath.c_str() );
    }
};

class ProduceOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "cartography:produce"; }
    std::string displayName() const override { return "Produce Finished Map Delivery"; }
    std::string group() const override { return "cartography"; }
    std::string description() const override
    {
        return "One-call governed production: upgrade → validate → compose → bounded repair "
               "→ export (single or atlas) → manifest sidecar, with atomic publish (a failed "
               "or cancelled delivery leaves the directory untouched). \"output\" is the "
               "manifest path (or the artifact when the manifest is disabled) — "
               "workflow-chainable.";
    }
    std::string determinismGrade() const override { return "bit_exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::UnsupportedForLargeRaster;
    }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        Json::Value props( Json::objectValue );
        Json::Value mapspec( Json::objectValue );
        mapspec["type"] = "object";
        props["mapspec"] = mapspec;
        props["directory"]["type"] = "string";
        props["format"]["type"] = "string";
        Json::Value formats( Json::arrayValue );
        formats.append( "png" );
        formats.append( "pdf" );
        formats.append( "svg" );
        props["format"]["enum"] = formats;
        props["file_name"]["type"] = "string";
        props["dpi"]["type"] = "number";
        props["write_manifest"]["type"] = "boolean";
        props["require_preflight_pass"]["type"] = "boolean";
        props["max_repair_iterations"]["type"] = "integer";
        schema["properties"] = props;
        Json::Value required( Json::arrayValue );
        required.append( "mapspec" );
        required.append( "directory" );
        schema["required"] = required;
        return schema;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
        requireQgisHost();
        context.throwIfCancelled();
        if ( !params.isMember( "mapspec" ) || !params["mapspec"].isObject() )
            throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                                   "Missing required parameter: mapspec (object)" );
        if ( !params.isMember( "directory" ) || !params["directory"].isString() ||
             params["directory"].asString().empty() )
            throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                                   "Missing required parameter: directory (non-empty string)" );

        ProduceRequest request;
        request.mapspec = params["mapspec"];
        request.directory = params["directory"].asString();
        if ( params.isMember( "format" ) && params["format"].isString() )
            request.format = params["format"].asString();
        if ( params.isMember( "file_name" ) && params["file_name"].isString() )
            request.file_name = params["file_name"].asString();
        if ( params.isMember( "dpi" ) && params["dpi"].isNumeric() )
            request.dpi = params["dpi"].asDouble();
        if ( params.isMember( "write_manifest" ) && params["write_manifest"].isBool() )
            request.write_manifest = params["write_manifest"].asBool();
        if ( params.isMember( "require_preflight_pass" ) &&
             params["require_preflight_pass"].isBool() )
            request.require_preflight_pass = params["require_preflight_pass"].asBool();
        if ( params.isMember( "max_repair_iterations" ) &&
             params["max_repair_iterations"].isInt() )
            request.max_repair_iterations = params["max_repair_iterations"].asInt();

        // Progress rides the operator's own reporting seam; cooperative
        // cancellation polls the context on every stage/page boundary.
        ProduceReporter reporter = [ &context ]( const char *stage, double progress,
                                                 const std::string &detail ) {
            context.reportProgress( progress, std::string( stage ) + ": " + detail );
            return !context.isCancelled();
        };

        context.reportProgress( 0.05, "produce: starting" );
        const ProduceResult result = produceMap( request, reporter );
        if ( !result.ok )
        {
            const ErrorCode code =
              result.cancelled()
                ? ErrorCode::Cancelled
                : ( result.error_code == "EXPORT_FAILED" ||
                        result.error_code == "COMPILE_FAILED" ||
                        result.error_code == "MANIFEST_FAILED"
                      ? ErrorCode::QgisProcessingError
                      : ErrorCode::InvalidInputData );
            throw RSOperatorError( code, result.error, produceResultToJson( result ) );
        }

        Json::Value out = produceResultToJson( result );
        const std::string delivered =
          out.isMember( "output" ) ? out["output"].asString() : std::string();
        return withOutput( std::move( out ), delivered.c_str() );
    }
};

} // namespace

void initCartographyOperators()
{
    // Register through the under-construction pointer when the family init
    // runs INSIDE the registry's call_once chain (defensive: hosts currently
    // call it after instance()), else through instance() post-chain.
    auto &registry = sicnu::operators::RSOperatorRegistry::instance();

    const auto add = [&registry]( const char *name, auto factory ) {
        if ( !registry.hasOperator( name ) )
            registry.registerOperator( name, std::move( factory ) );
    };
    add( "cartography:compose", [] { return std::make_unique<ComposeOperator>(); } );
    add( "cartography:preflight", [] { return std::make_unique<PreflightOperator>(); } );
    add( "cartography:validate", [] { return std::make_unique<ValidateOperator>(); } );
    add( "cartography:repair", [] { return std::make_unique<RepairOperator>(); } );
    add( "cartography:export", [] { return std::make_unique<ExportOperator>(); } );
    add( "cartography:produce", [] { return std::make_unique<ProduceOperator>(); } );
}

} // namespace sicnu::agent::cartography
