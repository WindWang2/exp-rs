// tests/test_workflow_facts_11.cpp
//
// Scientific Workflow Compiler & Grounding 11.0: fact model 2.0 contract —
// time parsing/cadence, spatial resolution/extent, quality masks, product
// generation, model task, and resource facts, each with per-key provenance.
//
// Oracle independence: every numeric expectation below is HAND-COMPUTED from
// public calendar/time facts (UTC epoch seconds, day counts) — none is
// derived by calling the implementation under test.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <string>
#include <vector>

#include "agent/contracts/spatial_contracts.h"
#include "agent/harness/workflow_facts.h"

using namespace sicnu::agent::harness;

namespace
{
Json::Value parse( const std::string &text )
{
  Json::Value out;
  Json::Reader reader;
  REQUIRE( reader.parse( text, out ) );
  return out;
}

Json::Value dateArray( const std::vector<std::string> &dates )
{
  Json::Value out( Json::arrayValue );
  for ( const std::string &date : dates )
    out.append( date );
  return out;
}
} // namespace

// ---------------------------------------------------------------------------
// Time parsing — epoch truth from UTC arithmetic.
// 2024-01-01T00:00:00Z == 1704067200 (known constant).
// ---------------------------------------------------------------------------

TEST_CASE( "parseInstant: date-only is midnight UTC, never fabricated precision", "[facts11][time]" )
{
  const wfacts::ParsedInstant parsed = wfacts::parseInstant( "2024-01-05" );
  REQUIRE( parsed.ok );
  REQUIRE( parsed.dateOnly );
  // 1704067200 + 4 * 86400 = 1704412800
  REQUIRE( parsed.epochSeconds == 1704412800LL );
  REQUIRE( wfacts::formatInstant( parsed.epochSeconds, true ) == "2024-01-05" );
}

TEST_CASE( "parseInstant: explicit Z, naive-as-UTC, and numeric offsets agree", "[facts11][time]" )
{
  // 2024-06-01T00:00:00Z = 1704067200 + 152 * 86400 = 1717200000
  const long long june1 = 1717200000LL;
  const wfacts::ParsedInstant zulu = wfacts::parseInstant( "2024-06-01T12:00:00Z" );
  REQUIRE( zulu.ok );
  REQUIRE( !zulu.dateOnly );
  REQUIRE( zulu.epochSeconds == june1 + 12 * 3600 );
  REQUIRE( zulu.tz == wfacts::TzAssumption::Explicit );

  const wfacts::ParsedInstant naive = wfacts::parseInstant( "2024-06-01 12:00:00" );
  REQUIRE( naive.ok );
  REQUIRE( naive.epochSeconds == june1 + 12 * 3600 );
  REQUIRE( naive.tz == wfacts::TzAssumption::NaiveAsUtc );

  // +02:00 means local is AHEAD: UTC instant is two hours EARLIER.
  const wfacts::ParsedInstant plusTwo = wfacts::parseInstant( "2024-06-01T12:00:00+02:00" );
  REQUIRE( plusTwo.ok );
  REQUIRE( plusTwo.epochSeconds == june1 + 10 * 3600 );
  REQUIRE( plusTwo.tz == wfacts::TzAssumption::Explicit );

  const wfacts::ParsedInstant plusTwoCompact = wfacts::parseInstant( "2024-06-01T12:00:00+0200" );
  REQUIRE( plusTwoCompact.ok );
  REQUIRE( plusTwoCompact.epochSeconds == june1 + 10 * 3600 );

  const wfacts::ParsedInstant minusFive = wfacts::parseInstant( "2024-06-01T12:00:00-05:00" );
  REQUIRE( minusFive.ok );
  REQUIRE( minusFive.epochSeconds == june1 + 17 * 3600 );

  // Fractional seconds parse; second-granularity epoch keeps the second.
  const wfacts::ParsedInstant fraction =
    wfacts::parseInstant( "2024-06-01T12:00:00.500Z" );
  REQUIRE( fraction.ok );
  REQUIRE( fraction.epochSeconds == june1 + 12 * 3600 );
}

TEST_CASE( "parseInstant: garbage is typed-unknown, never a guess", "[facts11][time]" )
{
  for ( const char *bad : { "not-a-date", "2024-13-40", "20240601", "", "2024-06",
                            "01/06/2024" } )
  {
    INFO( bad );
    REQUIRE_FALSE( wfacts::parseInstant( bad ).ok );
  }
}

// ---------------------------------------------------------------------------
// Cadence facts — hand-computed gaps and labels.
// ---------------------------------------------------------------------------

TEST_CASE( "cadence: exact 16-day acquisitions are regular with an Nd label", "[facts11][time]" )
{
  // Gaps: 16d, 16d, 16d — median 16d = 1382400s.
  const wfacts::TemporalCadenceFacts cadence = wfacts::temporalCadenceFromDates(
    dateArray( { "2024-01-01", "2024-01-17", "2024-02-02", "2024-02-18" } ) );
  REQUIRE( cadence.count == 4 );
  REQUIRE( cadence.regularity == wfacts::regularity::kRegular );
  REQUIRE( cadence.cadenceDays == 16.0 );
  REQUIRE( cadence.cadenceLabel == "16d" );
  REQUIRE( cadence.gapDeviations == 0 );
  REQUIRE( cadence.spanSeconds == 48LL * 86400 );
  REQUIRE( cadence.first == "2024-01-01" );
  REQUIRE( cadence.last == "2024-02-18" );
  REQUIRE_FALSE( cadence.truncated );
  REQUIRE( cadence.unparseable.empty() );

  const Json::Value wire = cadence.toJson( "observed" );
  REQUIRE( wire["fact_status"]["regularity"].asString() == "derived" );
  REQUIRE( wire["fact_status"]["first"].asString() == "observed" );
  REQUIRE( wire["fact_status"]["cadence_days"].asString() == "derived" );
}

TEST_CASE( "cadence: calendar-monthly series is monthly even across leap February", "[facts11][time]" )
{
  // 2024 is a leap year: gaps 31d, 29d, 31d — identical-gap regularity fails,
  // but each pair advances exactly one month at a stable day-of-month.
  const wfacts::TemporalCadenceFacts cadence = wfacts::temporalCadenceFromDates(
    dateArray( { "2024-01-15", "2024-02-15", "2024-03-15", "2024-04-15" } ) );
  REQUIRE( cadence.count == 4 );
  REQUIRE( cadence.regularity == wfacts::regularity::kNearRegular );
  REQUIRE( cadence.gapDeviations == 0 );
  REQUIRE( cadence.cadenceLabel == "monthly" );
}

TEST_CASE( "cadence: one dense + one long gap is irregular", "[facts11][time]" )
{
  // Gaps: 1d, 59d, 1d — median 1d; the 59d gap is outside [0.75,1.25]x.
  const wfacts::TemporalCadenceFacts cadence = wfacts::temporalCadenceFromDates(
    dateArray( { "2024-01-01", "2024-01-02", "2024-03-01", "2024-03-02" } ) );
  REQUIRE( cadence.count == 4 );
  REQUIRE( cadence.regularity == wfacts::regularity::kIrregular );
  REQUIRE( cadence.gapDeviations == 1 );
  REQUIRE( cadence.cadenceLabel.empty() );
}

TEST_CASE( "cadence: single, none, unknown and unparseable stay honest", "[facts11][time]" )
{
  const wfacts::TemporalCadenceFacts single =
    wfacts::temporalCadenceFromDates( dateArray( { "2024-05-05T10:00:00Z" } ) );
  REQUIRE( single.count == 1 );
  REQUIRE( single.regularity == wfacts::regularity::kSingle );
  REQUIRE( single.spanSeconds == 0 );

  const wfacts::TemporalCadenceFacts empty = wfacts::temporalCadenceFromDates( dateArray( {} ) );
  REQUIRE( empty.regularity == wfacts::regularity::kNone );

  const wfacts::TemporalCadenceFacts notArray =
    wfacts::temporalCadenceFromDates( Json::Value( Json::objectValue ) );
  REQUIRE( notArray.regularity == wfacts::regularity::kUnknown );
  REQUIRE( notArray.toJson( "observed" )["fact_status"]["regularity"].asString() == "unknown" );

  // A declared timestamp that cannot parse is LISTED, not dropped.
  const wfacts::TemporalCadenceFacts mixed =
    wfacts::temporalCadenceFromDates( dateArray( { "garbage", "2024-01-01" } ) );
  REQUIRE( mixed.count == 1 );
  REQUIRE( mixed.unparseable.size() == 1 );
  REQUIRE( mixed.unparseable.front() == "garbage" );
  REQUIRE( mixed.toJson( "observed" )["unparseable"].size() == 1 );
}

TEST_CASE( "cadence: dedup keeps the coarsest precision for a repeated instant", "[facts11][time]" )
{
  const wfacts::TemporalCadenceFacts cadence = wfacts::temporalCadenceFromDates(
    dateArray( { "2024-05-05T10:00:00Z", "2024-05-05" } ) );
  REQUIRE( cadence.count == 1 );
  REQUIRE( cadence.first == "2024-05-05" ); // date-only form wins
}

TEST_CASE( "cadence: over-bounds input truncates honestly", "[facts11][time]" )
{
  Json::Value dates( Json::arrayValue );
  for ( int i = 0; i < wfacts::FactsLimits::kMaxDates + 7; ++i )
  {
    // Distinct days so nothing dedups.
    const long long epoch = 1704067200LL + static_cast<long long>( i ) * 86400;
    dates.append( wfacts::formatInstant( epoch, true ) );
  }
  const wfacts::TemporalCadenceFacts cadence = wfacts::temporalCadenceFromDates( dates );
  REQUIRE( cadence.truncated );
  REQUIRE( cadence.count == wfacts::FactsLimits::kMaxDates );
}

TEST_CASE( "cadence from understanding: single scene + folded arrays", "[facts11][time]" )
{
  Json::Value understanding = parse( R"({
    "acquisition_time": "2024-03-10T10:31:00Z"
  })" );
  const wfacts::TemporalCadenceFacts single = wfacts::temporalCadenceFromUnderstanding( understanding );
  REQUIRE( single.count == 1 );
  REQUIRE( single.regularity == wfacts::regularity::kSingle );

  understanding["dates"] = dateArray( { "2024-03-10", "2024-03-26", "2024-04-11" } );
  const wfacts::TemporalCadenceFacts folded = wfacts::temporalCadenceFromUnderstanding( understanding );
  REQUIRE( folded.count == 4 ); // scene time + 3 folded
  // 16d gaps from 2024-03-10: regular.
  REQUIRE( folded.regularity == wfacts::regularity::kRegular );
  REQUIRE( folded.cadenceLabel == "16d" );
}

TEST_CASE( "collection descriptor: scene times are read, path-only scenes skipped", "[facts11][time]" )
{
  Json::Value descriptor = parse( R"({
    "scenes": [
      { "path": "s1.tif", "time": "2024-01-01" },
      { "path": "s2.tif" },
      { "path": "s3.tif", "acquisition_time": "2024-02-01" },
      "s4.tif"
    ]
  })" );
  Json::Value dates = wfacts::temporalDatesFromCollectionDescriptor( descriptor );
  REQUIRE( dates.isArray() );
  REQUIRE( dates.size() == 2 );

  REQUIRE( wfacts::temporalDatesFromCollectionDescriptor( Json::Value() ).isNull() );
  REQUIRE( wfacts::temporalDatesFromCollectionDescriptor( parse( R"({"scenes": []})" ) ).isArray() );
}

// ---------------------------------------------------------------------------
// Spatial resolution / extent.
// ---------------------------------------------------------------------------

TEST_CASE( "resolution: projected metre WKT yields metre classes at closed thresholds", "[facts11][resolution]" )
{
  const std::string utmWkt =
    "PROJCS[\"WGS 84 / UTM zone 33N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
    "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
    "UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
    "PARAMETER[\"latitude_of_origin\",0],UNIT[\"metre\",1],AUTHORITY[\"EPSG\",\"32633\"]]";
  auto understanding = [&]( double x, double y ) {
    Json::Value doc = parse( R"({"pixel_size": {"x": 0, "y": 0}})" );
    doc["pixel_size"]["x"] = x;
    doc["pixel_size"]["y"] = y;
    doc["crs"]["authid"] = "EPSG:32633";
    doc["crs"]["wkt"] = utmWkt;
    return doc;
  };
  REQUIRE( wfacts::spatialResolutionFacts( understanding( 10, 10 ) ).resolutionClass ==
           wfacts::resolution_class::kFine );
  REQUIRE( wfacts::spatialResolutionFacts( understanding( 16.5, 16.5 ) ).resolutionClass ==
           wfacts::resolution_class::kMedium );
  REQUIRE( wfacts::spatialResolutionFacts( understanding( 30, 30 ) ).resolutionClass ==
           wfacts::resolution_class::kMedium );
  REQUIRE( wfacts::spatialResolutionFacts( understanding( 30.5, 30.5 ) ).resolutionClass ==
           wfacts::resolution_class::kCoarse );

  const Json::Value wire = wfacts::spatialResolutionFacts( understanding( 10, 10 ) ).toJson();
  REQUIRE( wire["crs_unit"].asString() == "metre" );
  REQUIRE( wire["fact_status"]["resolution_class"].asString() == "derived" );
  REQUIRE( wire["fact_status"]["pixel_size"].asString() == "observed" );
}

TEST_CASE( "resolution: geographic degrees stay unknown_meters — no silent conversion", "[facts11][resolution]" )
{
  Json::Value doc = parse( R"({
    "pixel_size": {"x": 0.0001, "y": 0.0001},
    "crs": "EPSG:4326"
  })" );
  const wfacts::ResolutionFacts facts = wfacts::spatialResolutionFacts( doc );
  REQUIRE( facts.crsUnit == "degree" );
  REQUIRE( facts.resolutionClass == wfacts::resolution_class::kUnknownMeters );
  REQUIRE( facts.toJson()["fact_status"]["resolution_class"].asString() == "derived" );
}

TEST_CASE( "resolution: GEOGCS WKT carries degree units", "[facts11][resolution]" )
{
  Json::Value doc = parse( R"({
    "pixel_size": {"x": 0.00025, "y": 0.00025},
    "crs": {"authid": "user", "wkt": "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\"],"
            "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]"}
  })" );
  REQUIRE( wfacts::spatialResolutionFacts( doc ).crsUnit == "degree" );
}

TEST_CASE( "resolution: anisotropy, malformed extent, and absent pixel size", "[facts11][resolution]" )
{
  Json::Value aniso = parse( R"({
    "pixel_size": {"x": 10, "y": 20},
    "crs": {"authid": "EPSG:32633", "wkt": "PROJCS[\"x\",GEOGCS[\"g\",UNIT[\"degree\",1]],UNIT[\"metre\",1]]"}
  })" );
  const wfacts::ResolutionFacts facts = wfacts::spatialResolutionFacts( aniso );
  REQUIRE( facts.anisotropic );

  Json::Value inverted = parse( R"({
    "pixel_size": {"x": 10, "y": 10},
    "crs": "EPSG:32633",
    "extent": {"minX": 100, "maxX": 0, "minY": 0, "maxY": 10}
  })" );
  const wfacts::ResolutionFacts bad = wfacts::spatialResolutionFacts( inverted );
  REQUIRE( bad.extentPresent );
  REQUIRE_FALSE( bad.extentValid );
  REQUIRE( bad.toJson()["extent"]["valid"].asBool() == false );

  const wfacts::ResolutionFacts absent =
    wfacts::spatialResolutionFacts( parse( R"({"crs": "EPSG:32633"})" ) );
  REQUIRE( absent.resolutionClass == wfacts::resolution_class::kUnknown );
  REQUIRE( absent.toJson()["fact_status"]["pixel_size"].asString() == "unknown" );
}

// ---------------------------------------------------------------------------
// Quality masks — and the vocabulary pin against the producer side.
// ---------------------------------------------------------------------------

TEST_CASE( "quality masks: roles normalize, foreign roles and bad band indices flag", "[facts11][masks]" )
{
  Json::Value doc = parse( R"({
    "band_count": 3,
    "quality_masks": [
      {"band": 0, "role": "cloud"},
      {"band": 1, "role": "QA"},
      {"band": 2, "role": "nir"},
      {"band": 7, "role": "cloud_mask"}
    ]
  })" );
  const wfacts::QualityMaskFacts facts = wfacts::qualityMaskFacts( doc );
  REQUIRE( facts.present );
  REQUIRE( facts.count == 3 );
  REQUIRE( facts.roles == std::vector<std::string>( { "cloud", "qa", "cloud_mask" } ) );
  REQUIRE( facts.bandOutOfRange ); // band 7 >= band_count 3
  REQUIRE_FALSE( facts.truncated );
}

TEST_CASE( "quality masks: absent stays absent with unknown status", "[facts11][masks]" )
{
  const wfacts::QualityMaskFacts facts =
    wfacts::qualityMaskFacts( parse( R"({"band_count": 2})" ) );
  REQUIRE_FALSE( facts.present );
  const Json::Value wire = facts.toJson();
  REQUIRE( wire["fact_status"]["roles"].asString() == "unknown" );
}

TEST_CASE( "mask role vocabulary stays pinned to the understanding producer", "[facts11][masks]" )
{
  // spatial_contracts builds quality_masks from raster-inspect band roles
  // with its own (producer-side) mask-role list; every role it classifies as
  // a mask must also be recognized by the harness predicate — a one-sided
  // vocabulary would silently strand mask bands outside the fact model.
  Json::Value inspect;
  inspect["path"] = "fake.tif";
  int index = 0;
  for ( const char *role : { "mask", "qa", "quality", "cloud", "cloud_mask",
                             "cloud_and_shadow", "cloud_shadow", "snow", "validity" } )
  {
    Json::Value band;
    band["index"] = index++;
    band["role"] = role;
    inspect["bands"].append( band );
  }
  const Json::Value understanding =
    sicnu::agent::contracts::datasetUnderstandingFromRasterInspect( inspect );
  REQUIRE( understanding.isMember( "quality_masks" ) );
  const wfacts::QualityMaskFacts facts = wfacts::qualityMaskFacts( understanding );
  REQUIRE( facts.count == index );
  for ( const std::string &role : facts.roles )
    REQUIRE( wfacts::isMaskRoleName( role ) );
}

// ---------------------------------------------------------------------------
// Product generation.
// ---------------------------------------------------------------------------

TEST_CASE( "product generation: level parse is mechanical, never radiometric", "[facts11][product]" )
{
  auto check = []( const std::string &level, int expectedGeneration,
                   const std::string &expectedSuffix ) {
    Json::Value doc;
    doc["processing_level"] = level;
    const wfacts::ProductGenerationFacts facts = wfacts::productGenerationFacts( doc );
    INFO( level );
    REQUIRE( facts.generationLevel == expectedGeneration );
    REQUIRE( facts.levelSuffix == expectedSuffix );
  };
  check( "L2A", 2, "a" );
  check( "Level-1C", 1, "c" );
  check( "2A", 2, "a" );
  check( "l1", 1, "" );
  check( "processing level 3", 3, "" );

  Json::Value doc;
  doc["processing_level"] = "oper";
  const wfacts::ProductGenerationFacts unknown = wfacts::productGenerationFacts( doc );
  REQUIRE( unknown.generationLevel == -1 );
  REQUIRE( unknown.toJson()["fact_status"]["generation_level"].asString() == "unknown" );

  const wfacts::ProductGenerationFacts absent =
    wfacts::productGenerationFacts( parse( R"({"product_type": "S2MSI"})" ) );
  REQUIRE( absent.generationLevel == -1 );
  REQUIRE( absent.toJson()["fact_status"]["processing_level"].asString() == "unknown" );
}

// ---------------------------------------------------------------------------
// Model task.
// ---------------------------------------------------------------------------

TEST_CASE( "model task: free-form catalog strings normalize into the closed family", "[facts11][model]" )
{
  auto family = []( const std::string &raw ) {
    Json::Value doc;
    doc["task"] = raw;
    return wfacts::modelTaskFacts( doc ).taskFamily;
  };
  REQUIRE( family( "segmentation" ) == wfacts::model_task::kSegmentation );
  REQUIRE( family( "Segmentation" ) == wfacts::model_task::kSegmentation );
  REQUIRE( family( "building extraction" ) == wfacts::model_task::kExtraction );
  REQUIRE( family( "change-detection" ) == wfacts::model_task::kChangeDetection );
  REQUIRE( family( "change_detection" ) == wfacts::model_task::kChangeDetection );
  REQUIRE( family( "object detection" ) == wfacts::model_task::kDetection );
  REQUIRE( family( "quantum soup" ) == wfacts::model_task::kOther );
}

TEST_CASE( "model task: contract keys pass through with provenance", "[facts11][model]" )
{
  const Json::Value contract = parse( R"({
    "task": "segmentation",
    "readiness": "ready",
    "compatible": true,
    "estimated_cost": {"estimated_ram_mb": 4096, "gpu_accelerated": true}
  })" );
  const wfacts::ModelTaskFacts facts = wfacts::modelTaskFacts( contract );
  REQUIRE( facts.rawTask == "segmentation" );
  REQUIRE( facts.taskFamily == wfacts::model_task::kSegmentation );
  REQUIRE( facts.readiness == "ready" );
  REQUIRE( facts.compatibleDeclared );
  REQUIRE( facts.compatible );
  REQUIRE( facts.estimatedRamMb == 4096 );
  REQUIRE( facts.gpu );

  const Json::Value wire = facts.toJson();
  REQUIRE( wire["fact_status"]["task_family"].asString() == "derived" );
  REQUIRE( wire["fact_status"]["estimated_ram_mb"].asString() == "observed" );

  const wfacts::ModelTaskFacts absent = wfacts::modelTaskFacts( parse( R"({"task": ""})" ) );
  REQUIRE( absent.rawTask.empty() );
  const Json::Value absentWire = absent.toJson();
  REQUIRE( absentWire["task_family_effective"].asString() == wfacts::model_task::kOther );
  REQUIRE( absentWire["fact_status"]["task_family_effective"].asString() == "assumed" );
  REQUIRE( absentWire["fact_status"]["task_family"].asString() == "unknown" );
}

// ---------------------------------------------------------------------------
// Resource facts.
// ---------------------------------------------------------------------------

TEST_CASE( "resource facts: budget verdict only when BOTH sides are known", "[facts11][resource]" )
{
  const Json::Value cost = parse( R"({"estimated_ram_mb": 2048, "gpu_accelerated": true})" );
  const Json::Value budget = parse( R"({"max_ram_mb": 1024})" );
  const wfacts::ResourceFacts facts = wfacts::resourceFacts( 512, cost, budget, "gpu" );
  REQUIRE( facts.nodeEstimateMb == 512 );
  REQUIRE( facts.capabilityDemandMb == 2048 );
  REQUIRE( facts.expectationsMaxRamMb == 1024 );
  REQUIRE( facts.device == "gpu" );
  REQUIRE( facts.budgetKnown );
  REQUIRE( facts.overBudget );
  REQUIRE( facts.toJson()["fact_status"]["over_budget"].asString() == "derived" );

  // No budget declared: NOT over budget — the honest answer is "unknown",
  // and the verdict belongs to the analysis layer.
  const wfacts::ResourceFacts unbudgeted =
    wfacts::resourceFacts( 512, cost, Json::Value( Json::objectValue ), "" );
  REQUIRE_FALSE( unbudgeted.budgetKnown );
  REQUIRE_FALSE( unbudgeted.overBudget );
  REQUIRE( unbudgeted.toJson()["fact_status"]["over_budget"].asString() == "unknown" );
  REQUIRE( unbudgeted.toJson()["fact_status"]["device"].asString() == "unknown" );
}

// ---------------------------------------------------------------------------
// Bounds mirror + digest determinism.
// ---------------------------------------------------------------------------

TEST_CASE( "facts limits table matches its machine-readable mirror", "[facts11][bounds]" )
{
  const Json::Value limits = wfacts::workflowFactsLimits();
  REQUIRE( limits["max_dates"].asInt() == wfacts::FactsLimits::kMaxDates );
  REQUIRE( limits["max_masks"].asInt() == wfacts::FactsLimits::kMaxMasks );
  REQUIRE( limits["max_text_chars"].asInt() == wfacts::FactsLimits::kMaxTextChars );
  REQUIRE( limits["max_scenes"].asInt() == wfacts::FactsLimits::kMaxScenes );
}

TEST_CASE( "facts digest: identical facts, identical digest; different, different", "[facts11][digest]" )
{
  const Json::Value facts = parse( R"({"regularity": "regular", "cadence_days": 16.0})" );
  const std::string digest = wfacts::workflowFactsDigest( facts );
  REQUIRE( digest.size() == 16 );
  REQUIRE( digest == wfacts::workflowFactsDigest( facts ) );

  Json::Value mutated = facts;
  mutated["regularity"] = "irregular";
  REQUIRE( digest != wfacts::workflowFactsDigest( mutated ) );
}

TEST_CASE( "closed vocabularies reject typos", "[facts11][vocab]" )
{
  REQUIRE( wfacts::regularity::isKnownRegularity( wfacts::regularity::kNearRegular ) );
  REQUIRE_FALSE( wfacts::regularity::isKnownRegularity( "regular-ish" ) );
  REQUIRE( wfacts::resolution_class::isKnownResolutionClass(
    wfacts::resolution_class::kUnknownMeters ) );
  REQUIRE_FALSE( wfacts::resolution_class::isKnownResolutionClass( "ultra" ) );
  REQUIRE( wfacts::model_task::isKnownModelTask( wfacts::model_task::kChangeDetection ) );
  REQUIRE( wfacts::model_task::isKnownModelTask( wfacts::model_task::kOther ) );
  REQUIRE_FALSE( wfacts::model_task::isKnownModelTask( "detect" ) );
  REQUIRE( wfacts::isMaskRoleName( "Cloud_Shadow" ) );
  REQUIRE_FALSE( wfacts::isMaskRoleName( "nir" ) );
}
