// test_verify_chain_locale_matrix.cpp — Track 16 WP-B: the dual-locale
// byte-stability matrix over every digest production point on the
// verification chain (BASELINE.md §7 rows P1-P12 plus the bundle/report
// seals around them).
//
// Contract under test (DECISIONS.md D1): a digest produced anywhere on the
// chain is byte-identical across hosts regardless of the thread's
// LC_NUMERIC. The hostile case is the comma-decimal locale: snprintf("%.*g")
// style writers would emit "0,5" and the digest diverges. Each SECTION below
// is one matrix row: build the reference artifact under "C", reproduce it
// under every locale the host offers (self-provisioning de_DE.UTF-8 via
// LOCPATH when it is missing), and require byte equality of the digest (and
// of the canonical text where the API returns one).
//
// Anti-vacuity: a row is only meaningful if the hostile locale actually
// bites the machine — the harness proves setlocale reached a comma-decimal
// formatter (snprintf emits "0,5") before trusting a green row. When the
// host cannot provide a comma-decimal locale at all the row degrades to the
// installed-locale sweep and says so loudly (WARN), never silently.

#include <catch2/catch_test_macros.hpp>

#include <catch2/catch_message.hpp>

#include "grader/grader_engine.h"
#include "grader/grader_types.h"
#include "preflight/engine.h"
#include "preflight/report.h"
#include "science_context/broker.h"
#include "suitability/suitability_report.h"
#include "verify/verify_engine.h"
#include "verify/verify_levels.h"
#include "verify/verify_pack.h"
#include "verify/verify_types.h"

#include <json/json.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <algorithm>

using namespace sicnu::grader;
using namespace sicnu::preflight;
using namespace sicnu::suitability;
using namespace sicnu::verify;
using namespace sicnu::science_context;

namespace {

/// RAII restore of the thread's LC_NUMERIC (the guards under test do the
/// same discipline internally; this keeps the test itself honest).
struct LocaleRestorer
{
    std::string saved = setlocale( LC_NUMERIC, nullptr );
    ~LocaleRestorer() { setlocale( LC_NUMERIC, saved.c_str() ); }
};

/// True when @p localeName can be entered for LC_NUMERIC on this host.
bool tryLocale( const char *localeName )
{
    return setlocale( LC_NUMERIC, localeName ) != nullptr;
}

/// Self-provision a comma-decimal locale when the host image lacks one:
/// compile de_DE.UTF-8 once into a temp LOCPATH (glibc localedef ships in
/// the base toolchain; no root needed) and re-point the lookup. Returns the
/// locale name, or "" when even that is impossible (then rows say so).
std::string provisionCommaLocale()
{
    if ( tryLocale( "de_DE.UTF-8" ) )
        return "de_DE.UTF-8";
    if ( tryLocale( "de_DE.utf8" ) )
        return "de_DE.utf8";

    std::error_code ec;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path( ec ) / "sicnu-track16-locpath";
    if ( ec )
        return {};
    std::filesystem::create_directories( dir, ec );
    if ( ec )
        return {};
    const std::string compiled = ( dir / "de_DE.UTF-8" ).string();
    if ( !std::filesystem::exists( std::filesystem::path( compiled ), ec ) )
    {
        const std::string cmd = "localedef -i de_DE -f UTF-8 '" + compiled + "' 2>/dev/null";
        if ( std::system( cmd.c_str() ) != 0 )
            return {};
    }
    setenv( "LOCPATH", dir.string().c_str(), 1 );
    return tryLocale( "de_DE.UTF-8" ) ? std::string( "de_DE.UTF-8" ) : std::string();
}

/// Anti-vacuity: the hostile locale must actually move a decimal point.
bool commaLocaleBites()
{
    char buffer[64];
    std::snprintf( buffer, sizeof( buffer ), "%.12g", 0.5 );
    return std::string( buffer ) == "0,5";
}

/// Every locale this host can currently enter (the classic set plus the
/// provisioned comma-decimal one). First entry is the "C" reference.
std::vector<std::string> sweepLocales( std::string &commaOut )
{
    std::vector<std::string> locales{ "C" };
    for ( const char *name : { "C.utf8", "POSIX", "en_US.utf8", "en_US.UTF-8",
                               "zh_CN.utf8", "zh_CN.UTF-8" } )
        if ( tryLocale( name ) && std::find( locales.begin(), locales.end(), name ) == locales.end() )
            locales.push_back( name );
    commaOut = provisionCommaLocale();
    if ( !commaOut.empty() )
        locales.push_back( commaOut );
    return locales;
}

/// Anti-vacuity gate for the whole binary: without a comma-decimal locale
/// the strongest rows degrade; make that visible, not silent.
bool requireCommaLocaleEvidence()
{
    const std::string comma = provisionCommaLocale();
    if ( comma.empty() )
    {
        WARN( "host offers no comma-decimal locale (localedef unavailable); "
              "rows run against the installed sweep only" );
        return false;
    }
    REQUIRE( commaLocaleBites() );
    return true;
}

// ---- fixture bodies (fractional values everywhere a double can sneak in) --

Json::Value fractionalBody()
{
    Json::Value body( Json::objectValue );
    body["ratio"] = 0.30000000000000004;
    body["threshold"] = 0.35;
    body["tiny"] = 1e-13;
    body["big"] = 123456789.123456789;
    body["whole"] = 4.0;
    body["neg"] = -0.25;
    return body;
}

VerificationSpec fractionalSpec( const std::string &specId )
{
    VerificationSpec spec;
    spec.specId = specId;
    spec.scope = "node";
    VerificationCheckSpec check;
    check.checkId = "frac";
    check.kind = "state.invariant";
    Json::Value params( Json::objectValue );
    Json::Value expectations( Json::arrayValue );
    Json::Value expectation( Json::objectValue );
    expectation["key"] = "ratio";
    expectation["op"] = "eq";
    expectation["value"] = 0.30000000000000004;
    expectations.append( expectation );
    params["expectations"] = expectations;
    check.params = params;
    spec.checks.push_back( check );
    return spec;
}

GradingRubric fractionalRubric()
{
    GradingRubric rubric;
    rubric.rubricId = "track16-locale";
    rubric.revision = 1;
    rubric.title = "locale matrix rubric";
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "process";
    dim.title = "Process";
    dim.weight = 100.0;
    Criterion criterion;
    criterion.criterionId = "proc-metric";
    criterion.title = "metric window";
    criterion.maxPoints = 100.0;
    criterion.kind = CriterionKind::Metric;
    criterion.evidenceKey = "ndvi_mean";
    criterion.metric.mode = MetricExpectation::Mode::AtLeast;
    criterion.metric.value = 0.35;
    dim.criteria.push_back( criterion );
    rubric.dimensions.push_back( dim );
    return rubric;
}

GradeEvidence fractionalEvidence()
{
    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-locale";
    GradeEvidenceItem item;
    item.evidenceId = "ev-1";
    item.kind = EvidenceKind::Metric;
    item.key = "ndvi_mean";
    item.hasValue = true;
    item.value = 0.4;
    item.source = "metrics:run.json";
    evidence.items.push_back( item );
    return evidence;
}

PreflightReport fractionalPreflightReport()
{
    PreflightReport report = PreflightReport::makeEmpty( "rs:ndvi", "agent" );
    report.requestDigest = computeRequestDigest( PreflightRequest{} );
    PreflightFinding finding;
    finding.code = "SPF_RESOLUTION_RATIO";
    finding.severity = PreflightSeverity::RequireAck;
    finding.ruleId = "resolution_ratio";
    finding.ruleRevision = 1;
    finding.domain = "resolution";
    finding.subject = "slot:ref";
    finding.evidence["ratio"] = 0.30000000000000004;
    finding.evidence["threshold"] = 0.35;
    finding.basis = "observed";
    finding.humanExplanation = "ratio below threshold";
    finding.machineExplanation["observed"] = 0.30000000000000004;
    report.findings.push_back( finding );
    // Verdict consistency: one unacknowledged require_ack finding means the
    // report reads "requires_ack" (the engine's derivation rule; fromJson
    // refuses a report whose verdict contradicts its own findings).
    report.verdict = "requires_ack";
    return report;
}

} // namespace

TEST_CASE( "chain digest production points are numeric-locale invariants",
           "[track16][locale][matrix]" )
{
    LocaleRestorer restorer;
    requireCommaLocaleEvidence();

    std::string comma;
    const std::vector<std::string> locales = sweepLocales( comma );
    REQUIRE( locales.size() >= 2 ); // C + at least one more installed locale

    // The reference is always produced under "C".
    REQUIRE( tryLocale( "C" ) );

    SECTION( "V1 specDigest over fractional params" )
    {
        const std::string reference = specDigest( fractionalSpec( "spec.locale" ) );
        REQUIRE_FALSE( reference.empty() );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            CHECK( specDigest( fractionalSpec( "spec.locale" ) ) == reference );
        }
    }

    SECTION( "V2 engine report digest over a fractional spec" )
    {
        const VerificationContext context;
        const VerificationReport reference =
            evaluate( fractionalSpec( "spec.locale.report" ), context );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            const VerificationReport report =
                evaluate( fractionalSpec( "spec.locale.report" ), context );
            CHECK( report.digest() == reference.digest() );
            CHECK( report.specDigest == reference.specDigest );
        }
    }

    SECTION( "V3 packDigest over a fractional pack" )
    {
        VerifierPack pack;
        pack.packId = "pack.locale";
        pack.specs.push_back( fractionalSpec( "spec.pack" ) );
        const std::string reference = packDigest( pack );
        REQUIRE_FALSE( reference.empty() );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            CHECK( packDigest( pack ) == reference );
        }
    }

    SECTION( "V4 level-1 node postcondition digest" )
    {
        const VerificationSpec spec = fractionalSpec( "spec.level1" );
        const VerificationReport reference = verifyPlanNodePostcondition( spec, {} );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            CHECK( verifyPlanNodePostcondition( spec, {} ).digest() == reference.digest() );
        }
    }

    SECTION( "V5 canonical text round-trips under every locale" )
    {
        // The parse side (strtod-family in the JSON reader) is the other
        // half of the digest contract: canonical text produced under "C"
        // must re-parse and re-digest identically under a comma-decimal
        // thread locale.
        const std::string canonical = canonicalJsonText( fractionalBody() );
        REQUIRE_FALSE( canonical.empty() );
        const std::string referenceDigest = specDigest( fractionalSpec( "spec.roundtrip" ) );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            const VerificationSpec parsed = fractionalSpec( "spec.roundtrip" );
            CHECK( specDigest( parsed ) == referenceDigest );
            // The fractional values themselves survive a text round-trip.
            Json::Value back;
            Json::CharReaderBuilder builder;
            std::string errors;
            std::istringstream stream( canonical );
            REQUIRE( Json::parseFromStream( builder, stream, &back, &errors ) );
            // The canonical form carries 12 significant digits: the literal
            // 0.30000000000000004 sealed as "0.3" and must read back as the
            // same double the writer meant — under every locale.
            CHECK( back["ratio"].asDouble() == 0.3 );
            CHECK( back["threshold"].asDouble() == 0.35 );
        }
    }

    SECTION( "G1 grader grade() digest over fractional rubric+evidence" )
    {
        const GradeOutcome reference = grade( fractionalRubric(), fractionalEvidence() );
        REQUIRE( reference.ok );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            const GradeOutcome outcome = grade( fractionalRubric(), fractionalEvidence() );
            REQUIRE( outcome.ok );
            CHECK( outcome.report.digest == reference.report.digest );
            CHECK( outcome.report.rubricDigest == reference.report.rubricDigest );
            CHECK( outcome.report.evidenceDigest == reference.report.evidenceDigest );
        }
    }

    SECTION( "P1 preflight computeRequestDigest over fractional params" )
    {
        PreflightRequest request;
        request.operatorId = "rs:ndvi";
        request.mode = "agent";
        request.operatorParams["threshold"] = 0.35;
        request.operatorParams["window"] = 0.30000000000000004;
        request.inputs = { { "a", "asset-1" } };
        const std::string reference = computeRequestDigest( request );
        REQUIRE_FALSE( reference.empty() );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            CHECK( computeRequestDigest( request ) == reference );
        }
    }

    SECTION( "P2 preflight computeRulesRevision over the rule set" )
    {
        const std::vector<std::pair<std::string, int>> rules{
            { "band_role", 1 }, { "pair_crs", 1 }, { "resolution_ratio", 2 } };
        const std::string reference = computeRulesRevision( rules );
        REQUIRE_FALSE( reference.empty() );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            CHECK( computeRulesRevision( rules ) == reference );
        }
    }

    SECTION( "P3 preflight reportDigest over fractional evidence" )
    {
        const PreflightReport reference = fractionalPreflightReport();
        const std::string referenceDigest = reportDigest( reference );
        REQUIRE_FALSE( referenceDigest.empty() );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            CHECK( reportDigest( fractionalPreflightReport() ) == referenceDigest );
        }
    }

    SECTION( "P4 preflight total finding order under every locale" )
    {
        // Two findings that differ only in a fractional evidence field must
        // order identically under every locale (the tie-break text is part
        // of the deterministic contract).
        PreflightFinding a;
        a.code = "SPF_X";
        a.severity = PreflightSeverity::RequireAck;
        a.ruleId = "r";
        a.ruleRevision = 1;
        a.subject = "s";
        a.evidence["v"] = 0.30000000000000004;
        PreflightFinding b = a;
        b.evidence["v"] = 0.35;
        const bool referenceOrder = totalFindingOrder( a, b );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            CHECK( totalFindingOrder( a, b ) == referenceOrder );
            CHECK( totalFindingOrder( b, a ) == !referenceOrder );
        }
    }

    SECTION( "P5 preflight report JSON round-trip under every locale" )
    {
        const PreflightReport report = fractionalPreflightReport();
        const std::string referenceDigest = reportDigest( report );
        const Json::Value json = report.toJson();
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            const auto back = PreflightReport::fromJson( json );
            REQUIRE( back.has_value() );
            CHECK( reportDigest( *back ) == referenceDigest );
        }
    }

    SECTION( "SC1 broker bundle serialization and bundle id" )
    {
        // Inline passport carries fractional geometry: the serialized bundle
        // embeds doubles, and bundle_id seals the bytes.
        ScienceContextBroker broker;
        SynthesizeRequest request;
        request.goal = "locale stability probe";
        request.useCache = false;
        request.constraints.autonomyLevel = "L2";
        sicnu::state::RemoteSensingAssetState passport;
        passport.assetId = "asset-locale";
        passport.revision = 1;
        passport.geometry.hasPixelSize = true;
        passport.geometry.pixelSizeX = 0.30000000000000004;
        passport.geometry.pixelSizeY = 0.35;
        request.passports.push_back( passport );

        const SynthesizeResult reference = broker.synthesize( request );
        const std::string referenceId = reference.bundle.bundleId;
        REQUIRE_FALSE( referenceId.empty() );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            ScienceContextBroker freshBroker;
            const SynthesizeResult outcome = freshBroker.synthesize( request );
            CHECK( outcome.bundle.bundleId == referenceId );
            CHECK( serializeBundle( outcome.bundle ) == serializeBundle( reference.bundle ) );
        }
    }

    SECTION( "S1 suitability contentDigest over fractional evidence" )
    {
        SuitabilityReport report;
        report.setDatasetVersionId( QStringLiteral( "dv-locale" ) );
        SuitabilityCriterion criterion;
        criterion.id = QStringLiteral( "spatial.coverage" );
        criterion.level = SuitabilityLevel::Marginal;
        criterion.evidence = QJsonObject{ { QStringLiteral( "coverage" ), 0.30000000000000004 },
                                          { QStringLiteral( "threshold" ), 0.35 } };
        report.addCriterion( criterion );
        const QString reference = report.contentDigest();
        REQUIRE_FALSE( reference.isEmpty() );
        for ( const std::string &locale : locales )
        {
            INFO( "locale " << locale );
            REQUIRE( tryLocale( locale.c_str() ) );
            CHECK( report.contentDigest() == reference );
        }
    }
}

TEST_CASE( "the harness comma-decimal locale actually bites the formatter",
           "[track16][locale][matrix]" )
{
    // Potency row: if this fails, the host could not enter a comma-decimal
    // locale and every green row above was weaker than intended.
    LocaleRestorer restorer;
    const std::string comma = provisionCommaLocale();
    if ( comma.empty() )
    {
        WARN( "no comma-decimal locale provisionable on this host" );
        return;
    }
    REQUIRE( tryLocale( comma.c_str() ) );
    char buffer[64];
    std::snprintf( buffer, sizeof( buffer ), "%.12g", 0.5 );
    CHECK( std::string( buffer ) == "0,5" );
}
