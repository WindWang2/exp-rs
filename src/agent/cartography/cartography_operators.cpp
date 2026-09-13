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

/// Shared pre-compile pass: v3 conditions resolve BEFORE composition and
/// preflight (mirror of the compiler, so hidden items cannot produce false
/// positives), then anchors/constraints resolve into concrete rects.
Json::Value resolveCompositionPass( Json::Value spec )
{
    if ( spec.isObject() && spec.isMember( "condition_context" ) )
    {
        std::vector<std::string> conditionErrors;
        mapspec::resolveMapSpecConditions( spec, spec["condition_context"], &conditionErrors );
    }
    const double marginDefault =
        tokenNumber( resolveTokenSet( spec ), "spacing.margin_mm", 12.0 );
    return resolveComposition( spec, marginDefault ).toJson();
}

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
    std::string determinismGrade() const override { return "bit_exact"; }
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
        out["quality"] = preflightMapSpec( params["mapspec"] );
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
    std::string determinismGrade() const override { return "bit_exact"; }
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
    std::string determinismGrade() const override { return "bit_exact"; }
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
        Json::Value report( Json::objectValue );
        report["type"] = "object";
        report["description"] =
            "Optional preflight report; when absent one is computed from the mapspec";
        props["report"] = report;
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
        Json::Value spec = params["mapspec"];
        Json::Value report = params.isMember( "report" ) && params["report"].isObject()
                                 ? params["report"]
                                 : preflightMapSpec( spec );
        Json::Value ledger( Json::arrayValue );
        const int applied = repairMapSpecWithLedger( spec, report, &ledger );
        Json::Value out( Json::objectValue );
        out["mapspec"] = spec;
        out["applied"] = applied;
        out["ledger"] = ledger;
        out["quality_after"] = preflightMapSpec( spec );
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
        const std::string outputPath = out["path"].asString();
        return withOutput( std::move( out ), outputPath.c_str() );
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
}

} // namespace sicnu::agent::cartography
