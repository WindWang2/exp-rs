// tests/test_capability_knowledge.cpp
//
// D8 capability knowledge layer guard (ADR 0146). Deterministic, no CI, no
// model calls. Asserts the completion contract:
//   1. coverage: exactly 111/111 rs: operators carry a v2 capability sidecar
//      (binary — 110 is a failure), and every sidecar resolves live;
//   2. no dangling operator reference in the sidecars or the relation graph;
//   3. the relation graph is a DAG, exclusive pairs never double as chain
//      edges, and every family is populated;
//   4. derived fields byte-match a fresh derivation from the live
//      AlgorithmDescriptors (hand-editing derived facts is a build failure);
//   5. every operator carries an ADR 0124 determinism grade; tolerance /
//      stochastic operators are surfaced through the query API and in
//      composition notes;
//   6. modality/band-role facts agree with the preflight knowledge mirror
//      (data/agent/capabilities) — one fact, one value, two consumers;
//   7. agent-facing budgets: manifest page < 64 KiB, error catalog < 8 KiB;
//   8. pi/knowledge/capability-*.md regenerates with zero diff;
//   9. NDVI, bi-temporal change and supervised classification compose from
//      the capability graph alone (no LLM), with exact deterministic steps.

#include <catch2/catch_test_macros.hpp>

#include "agent/harness/capability_catalog.h"
#include "agent/harness/capability_graph.h"
#include "agent/harness/capability_knowledge.h"
#include "agent/harness/capability_pages.h"
#include "agent/harness/capability_relations.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "processing/framework/algorithm_meta_store.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <QFile>
#include <QString>

#include <algorithm>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace sicnu::agent::harness;

namespace {

constexpr const char *kSourceDir = CMAKE_SOURCE_DIR;

size_t compactBytes( const Json::Value &doc )
{
  Json::StreamWriterBuilder builder;
  builder[ "indentation" ] = "";
  return Json::writeString( builder, doc ).size();
}

std::string readFile( const std::string &path )
{
  QFile file( QString::fromStdString( path ) );
  if ( !file.open( QIODevice::ReadOnly ) )
    return "<missing: " + path + ">";
  return QString::fromUtf8( file.readAll() ).toStdString();
}

struct Bootstrap
{
    CapabilityCatalog &catalog;
    std::vector<std::string> registryIds;

    explicit Bootstrap()
      : catalog( CapabilityCatalog::instance() )
    {
        sicnu::operators::RSOperatorRegistry::instance();
        sicnu::operators::rs::initBuiltinRsOperators();
        sicnu::operators::rs::installRsOperatorProvider();
        sicnu::processing::AtomicAlgorithmRegistry::instance().initialize();

        catalog.setDirectory( std::string( kSourceDir ) +
                              "/data/processing/algorithm_meta/capability" );
        catalog.reload();

        CapabilityRelations::instance().setFilePath(
          std::string( kSourceDir ) +
          "/data/processing/algorithm_meta/capability/capability_relations.json" );
        CapabilityRelations::instance().reload();

        for ( const std::string &name :
              sicnu::operators::RSOperatorRegistry::instance().operatorNames() )
        {
          if ( name.rfind( "rs:", 0 ) == 0 )
            registryIds.push_back( name );
        }
        std::sort( registryIds.begin(), registryIds.end() );
    }

    std::set<std::string> registrySet() const { return { registryIds.begin(), registryIds.end() }; }
    std::set<std::string> catalogSet() const
    {
        const std::vector<std::string> ids = catalog.entryIds();
        return { ids.begin(), ids.end() };
    }
};

} // namespace

TEST_CASE( "D8 coverage: 111/111 rs operators carry capability metadata",
           "[capability][d8][coverage]" )
{
    Bootstrap boot;
    const std::set<std::string> registry = boot.registrySet();
    const std::set<std::string> catalog = boot.catalogSet();

    REQUIRE( registry.size() == 111 );
    REQUIRE( boot.catalog.loadProblems().empty() );
    REQUIRE( catalog.size() == 111 );

    for ( const std::string &id : registry )
    {
        if ( !catalog.count( id ) )
          FAIL( "operator missing capability metadata: " + id );
    }
    for ( const std::string &id : catalog )
    {
        if ( !registry.count( id ) )
          FAIL( "capability sidecar names an unregistered operator: " + id );
    }

    // Sidecar-level validation is fail-closed at load; loadProblems empty is
    // the contract, re-checked here so a regression names the problem.
    for ( const std::string &id : boot.catalog.entryIds() )
    {
        const std::vector<std::string> problems =
          CapabilityCatalog::validateEntry( boot.catalog.entry( id ) );
        for ( const std::string &problem : problems )
          FAIL( id + ": " + problem );
    }
}

TEST_CASE( "D8 family taxonomy: total, closed, non-empty", "[capability][d8][families]" )
{
    Bootstrap boot;
    size_t assigned = 0;
    for ( const std::string &family : capabilityFamilies() )
    {
        const std::vector<std::string> ids = boot.catalog.byFamily( family );
        REQUIRE_FALSE( ids.empty() );
        assigned += ids.size();
    }
    // Every operator appears in exactly one family.
    REQUIRE( assigned == boot.catalog.entryIds().size() );

    // The canonical map agrees with the loaded sidecars (a renamed family in
    // either place fails loudly instead of splitting the query surface).
    for ( const std::string &id : boot.catalog.entryIds() )
    {
        const Json::Value block = boot.catalog.capability( id );
        const std::string canonical = canonicalFamily(
          id, block[ "operator_group" ].isString() ? block[ "operator_group" ].asString()
                                                   : std::string() );
        REQUIRE( canonical == block[ "family" ].asString() );
    }
}

TEST_CASE( "D8 drift: derived sidecar fields match live descriptors",
           "[capability][d8][drift]" )
{
    Bootstrap boot;
    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    for ( const std::string &id : boot.catalog.entryIds() )
    {
        auto op = registry.create( id );
        REQUIRE( op != nullptr );
        const auto descriptor =
          sicnu::processing::AlgorithmDescriptorBuilder::buildFromRsOperator( *op );

        const Json::Value committed = boot.catalog.entry( id );
        const Json::Value derived = deriveCapabilityBlock( descriptor, committed[ "capability" ] );
        INFO( "derived capability block drifted for " + id
              + " — regenerate with: capability_knowledge_tool gen-meta" );
        REQUIRE( committed[ "capability" ] == derived );

        // The v1 overlay fields agree with the store's own derivation.
        const auto entry = sicnu::processing::AlgorithmMetaStore::entryFromDescriptor( descriptor );
        CHECK( committed[ "input" ] == Json::Value( entry.input ) );
        CHECK( committed[ "output" ] == Json::Value( entry.output ) );
        if ( !entry.task.empty() )
          CHECK( committed[ "task" ] == Json::Value( entry.task ) );
    }
}

TEST_CASE( "D8 determinism: every operator graded, non-reproducible surfaced",
           "[capability][d8][determinism]" )
{
    Bootstrap boot;
    std::vector<std::string> tolerance;
    std::vector<std::string> stochastic;
    for ( const std::string &id : boot.catalog.entryIds() )
    {
        const std::string grade = boot.catalog.determinismOf( id );
        REQUIRE( ( grade == "bit_exact" || grade == "tolerance" ) );
        if ( grade == "tolerance" )
          tolerance.push_back( id );
        if ( boot.catalog.isStochastic( id ) )
          stochastic.push_back( id );
    }
    // Pinned from the operators' explicit ADR 0124 declarations.
    for ( const char *id : { "rs:sar_speckle", "rs:temporal_smooth" } )
    {
        REQUIRE( std::find( tolerance.begin(), tolerance.end(), std::string( id ) ) !=
                 tolerance.end() );
    }
    REQUIRE( boot.catalog.stochasticOperators().size() == stochastic.size() );

    // Composition surfaces the reproducibility caveat for tolerance targets.
    const Json::Value composed = composeChain( "rs:temporal_smooth", Json::Value() );
    REQUIRE( composed[ "steps" ].isArray() );
    bool notesReproducibility = false;
    for ( const Json::Value &note : composed[ "notes" ] )
    {
        if ( note.isString() && note.asString().find( "tolerance" ) != std::string::npos )
          notesReproducibility = true;
    }
    CHECK( notesReproducibility );
}

TEST_CASE( "D8 knowledge mirror agreement: modality and band roles",
           "[capability][d8][mirror]" )
{
    Bootstrap boot;
    CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
    knowledge.setDirectory( std::string( kSourceDir ) + "/data/agent/capabilities" );
    knowledge.reload();
    REQUIRE( knowledge.loadProblems().empty() );

    for ( const std::string &id : boot.catalog.entryIds() )
    {
        const Json::Value mirror = knowledge.entryForOperator( id );
        if ( mirror.isNull() )
          continue; // mirror covers the preflight subset; D8 covers all 111
        const Json::Value block = boot.catalog.capability( id );
        INFO( "modality/band_roles disagree with the knowledge mirror for " + id
              + " — rerun capability_knowledge_tool gen-meta" );
        if ( mirror[ "modality" ].isArray() && !mirror[ "modality" ].empty() )
          CHECK( block[ "modality" ] == mirror[ "modality" ] );
        if ( mirror[ "band_roles" ].isObject() && !mirror[ "band_roles" ].empty() )
          CHECK( block[ "band_roles" ] == mirror[ "band_roles" ] );
    }
}

TEST_CASE( "D8 relation graph: no dangling refs, acyclic, exclusive consistent",
           "[capability][d8][graph]" )
{
    Bootstrap boot;
    auto &relations = CapabilityRelations::instance();
    REQUIRE( relations.loaded() );
    REQUIRE( relations.loadProblems().empty() );

    // Re-validate the committed document so a regression names the finding.
    std::ifstream in( std::string( kSourceDir ) +
                      "/data/processing/algorithm_meta/capability/capability_relations.json" );
    REQUIRE( in.is_open() );
    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    REQUIRE( Json::parseFromStream( builder, in, &doc, &errors ) );
    for ( const std::string &problem :
          validateRelations( doc, boot.registrySet() ) )
      FAIL( problem );

    // requires_shared_grid mirrors the sidecar contracts exactly.
    for ( const std::string &id : boot.catalog.entryIds() )
    {
        CHECK( relations.requiresGrid( id ) == boot.catalog.requiresGrid( id ) );
    }
    // The grid fixer itself must not demand an aligned grid.
    CHECK_FALSE( relations.requiresGrid( relations.gridFixer() ) );
}

TEST_CASE( "D8 budgets: manifest page < 64 KiB, error catalog < 8 KiB",
           "[capability][d8][budgets]" )
{
    Bootstrap boot;
    Json::Value page = boot.catalog.manifestPage( "", 1, 32 );
    CHECK( page[ "entries" ].size() <= 32 );
    size_t maxPageBytes = compactBytes( page );
    for ( const std::string &family : capabilityFamilies() )
    {
        const Json::Value familyPage = boot.catalog.manifestPage( family, 1, 64 );
        maxPageBytes = std::max( maxPageBytes, compactBytes( familyPage ) );
    }
    CHECK( maxPageBytes < CapabilityCatalog::kManifestBudgetBytes );
    WARN( "D8 max manifest page bytes: " << maxPageBytes );

    const Json::Value catalog = boot.catalog.errorCatalog();
    const size_t catalogBytes = compactBytes( catalog );
    CHECK( catalogBytes < CapabilityCatalog::kErrorCatalogBudgetBytes );
    WARN( "D8 error catalog bytes: " << catalogBytes );
}

TEST_CASE( "D8 knowledge pages regenerate with zero diff", "[capability][d8][pages]" )
{
    Bootstrap boot;
    int drift = 0;
    for ( const KnowledgePage &page : renderCapabilityKnowledgePages() )
    {
        const std::string committed =
          readFile( std::string( kSourceDir ) + "/" + page.relativePath );
        if ( committed != page.content )
        {
          FAIL( "page drifted: " + page.relativePath +
                " — regenerate with: capability_knowledge_tool gen-pages" );
          ++drift;
        }
    }
    CHECK( drift == 0 );
}

TEST_CASE( "D8 composition: NDVI / bi-temporal change / supervised classification "
           "from the graph alone",
           "[capability][d8][compose]" )
{
    Bootstrap boot;

    // NDVI on uncalibrated DN data: atmospheric correction first; on surface
    // reflectance the edge is gated off and the index runs directly.
    {
        Json::Value facts( Json::objectValue );
        facts[ "radiometric_state" ] = "digital_number";
        const Json::Value composed = composeChain( "rs:spectral_index", facts );
        REQUIRE( composed[ "error" ].isNull() );
        std::vector<std::string> steps;
        for ( const Json::Value &step : composed[ "steps" ] )
          steps.push_back( step[ "id" ].asString() );
        REQUIRE( steps == std::vector<std::string>{ "rs:atmospheric_correction",
                                                    "rs:spectral_index" } );
    }
    {
        Json::Value facts( Json::objectValue );
        facts[ "radiometric_state" ] = "surface_reflectance";
        const Json::Value composed = composeChain( "rs:spectral_index", facts );
        std::vector<std::string> steps;
        for ( const Json::Value &step : composed[ "steps" ] )
          steps.push_back( step[ "id" ].asString() );
        REQUIRE( steps == std::vector<std::string>{ "rs:spectral_index" } );
    }

    // Bi-temporal change: the shared-grid contract inserts rs:align unless
    // the facts declare the epochs aligned.
    {
        const Json::Value composed = composeChain( "rs:change_difference", Json::Value() );
        std::vector<std::string> steps;
        for ( const Json::Value &step : composed[ "steps" ] )
          steps.push_back( step[ "id" ].asString() );
        REQUIRE( steps == std::vector<std::string>{ "rs:align", "rs:change_difference" } );
    }
    {
        Json::Value facts( Json::objectValue );
        facts[ "aligned" ] = true;
        const Json::Value composed = composeChain( "rs:change_difference", facts );
        std::vector<std::string> steps;
        for ( const Json::Value &step : composed[ "steps" ] )
          steps.push_back( step[ "id" ].asString() );
        REQUIRE( steps == std::vector<std::string>{ "rs:change_difference" } );
    }

    // Supervised classification: PCA feature reduction joins the chain only
    // when the facts call for it; the target's prerequisites surface as notes.
    {
        Json::Value facts( Json::objectValue );
        facts[ "feature_reduction" ] = "pca";
        const Json::Value composed = composeChain( "rs:supervised_classification", facts );
        std::vector<std::string> steps;
        for ( const Json::Value &step : composed[ "steps" ] )
          steps.push_back( step[ "id" ].asString() );
        REQUIRE( steps == std::vector<std::string>{ "rs:pca", "rs:supervised_classification" } );

        const Json::Value block = boot.catalog.capability( "rs:supervised_classification" );
        size_t echoed = 0;
        for ( const Json::Value &prereq : block[ "prerequisites" ] )
        {
          for ( const Json::Value &note : composed[ "notes" ] )
          {
            if ( note.isString() && note.asString().find( prereq.asString() ) !=
                                      std::string::npos )
            {
              ++echoed;
              break;
            }
          }
        }
        CHECK( echoed == block[ "prerequisites" ].size() );
    }
    {
        Json::Value facts( Json::objectValue );
        facts[ "feature_reduction" ] = "none";
        const Json::Value composed = composeChain( "rs:supervised_classification", facts );
        std::vector<std::string> steps;
        for ( const Json::Value &step : composed[ "steps" ] )
          steps.push_back( step[ "id" ].asString() );
        REQUIRE( steps == std::vector<std::string>{ "rs:supervised_classification" } );
    }

    // Unknown targets are typed errors, never a guess.
    {
        const Json::Value composed = composeChain( "rs:does_not_exist", Json::Value() );
        CHECK( composed[ "error" ].asString() == "UNKNOWN_OPERATOR" );
    }

    // The composer is a real tool surface, not just a function.
    registerCapabilityGraphTools();
    auto tool = sicnu::agent::spatial_tools::SpatialToolRegistry::instance().find(
      "harness:compose_chain" );
    REQUIRE( tool != std::nullopt );
    Json::Value input;
    input[ "target" ] = "rs:change_difference";
    const auto result = ( *tool )->execute( input );
    REQUIRE( result.success );
    CHECK( result.output[ "steps" ].size() == 2 );
}

TEST_CASE( "D8 query API: bounded browsing over the closed families",
           "[capability][d8][query]" )
{
    Bootstrap boot;
    // Modality routing: every sar-family operator consumes sar inputs (a
    // subset — classification/inference ops also accept sar by design, so
    // byInputModality("sar") is a superset of the sar family).
    for ( const std::string &id : boot.catalog.byFamily( "sar" ) )
    {
        const Json::Value block = boot.catalog.capability( id );
        bool consumesSar = false;
        for ( const Json::Value &modality : block[ "modality" ] )
        {
            if ( modality.isString() && modality.asString() == "sar" )
              consumesSar = true;
        }
        CHECK( consumesSar );
    }
    CHECK( boot.catalog.byInputModality( "sar" ).size() >=
           boot.catalog.byFamily( "sar" ).size() );

    // Pages are deterministic and clamped.
    const Json::Value beyond = boot.catalog.manifestPage( "spectral", 999, 16 );
    CHECK( beyond[ "page" ].asInt() == beyond[ "page_count" ].asInt() );
    CHECK( compactBytes( boot.catalog.manifestPage( "spectral", 1, 16 ) ) ==
           compactBytes( boot.catalog.manifestPage( "spectral", 1, 16 ) ) );

    // chainFrom is browse-shaped and consistent with composeChain.
    const Json::Value links = chainFrom( "rs:spectral_index" );
    bool listsChangeNormalized = false;
    for ( const Json::Value &edge : links[ "downstream" ] )
    {
        if ( edge[ "to" ].asString() == "rs:change_normalized_difference" )
          listsChangeNormalized = true;
    }
    CHECK( listsChangeNormalized );
    bool listsAtmospheric = false;
    for ( const Json::Value &edge : links[ "upstream" ] )
    {
        if ( edge[ "from" ].asString() == "rs:atmospheric_correction" )
          listsAtmospheric = true;
    }
    CHECK( listsAtmospheric );
}
