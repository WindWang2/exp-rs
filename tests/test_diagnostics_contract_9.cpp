/***************************************************************************
 * test_diagnostics_contract_9.cpp
 *
 * Contract Platform 9.0 (M3) — error-taxonomy ↔ diagnostics catalog census.
 * Permanent guard for the #870 drift class:
 *
 *   1. enum drift: the `ErrorCode` enumerator set and the errorCodeToString
 *      switch must agree exactly (a new enum value without a string mapping
 *      is a bug);
 *   2. catalog census: every harness wire code and every operator error
 *      string must resolve to a curated diagnostics.json page or an explicit
 *      allow-list entry — DiagnosticCatalog::fallback() may not silently
 *      absorb new codes;
 *   3. harness taxonomy runtime totals: every scanned code produces a closed
 *      vocabulary category and retry class;
 *   4. preflight raw string codes (addWarning literals) are enumerated —
 *      the stringly-typed emission surface is pinned;
 *   5. census red-direction: the factored census query run against a
 *      catalog with one page removed reports exactly that code (helper-level
 *      proof; the live-tree census itself is the gate).
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include "contracts/error_code_scanner.h"

#include <agent/harness/harness_error.h>
#include <help/diagnostic_catalog.h>
#include <help/help_composition.h>
#include <help/help_id.h>
#include <help/help_registry.h>
#include <operators/framework/rs_operator_error.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

using sicnu::contracts::ErrorCodeReport;
using sicnu::contracts::ErrorCodeScanner;

namespace {

const char *kSourceDir = CMAKE_SOURCE_DIR;

std::string readFile( const std::string &path )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return {};
    return std::string( std::istreambuf_iterator<char>( in ),
                        std::istreambuf_iterator<char>() );
}

struct AllowEntry
{
    const char *code;
    const char *reason; // why no curated diagnostics page exists
};

// Rot guard note: PREFLIGHT_BLOCKED previously lived here; it gained a
// curated page, and the stale-entry check below/after the census caught it.
const std::vector<AllowEntry> kAllowedHarnessCodesWithoutPage = {
    // D9: TEACHING_REFUSAL is a contract refusal, not a malfunction — the
    // student-facing explanation travels in the refusal envelope
    // (lab_copilot teachingRefusalEnvelope), so no curated diagnostics page.
    { "TEACHING_REFUSAL",
      "contract refusal (lab teaching constraint); explained in-band by the "
      "copilot, not a diagnosable failure" },
};

const std::vector<AllowEntry> kAllowedOperatorCodesWithoutPage = {
    // { "code", "reason" }
};

bool allowed( const std::vector<AllowEntry> &list, const std::string &code )
{
    return std::any_of( list.begin(), list.end(),
                        [&]( const AllowEntry &e ) { return code == e.code; } );
}

/// family → code → curated page id from data/help/diagnostics.json.
std::map<std::string, std::map<std::string, std::string>> catalogByFamily()
{
    std::map<std::string, std::map<std::string, std::string>> out;
    QFile f( QStringLiteral( "%1/data/help/diagnostics.json" )
                 .arg( QStringLiteral( CMAKE_SOURCE_DIR ) ) );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson( f.readAll(), &pe );
    REQUIRE( pe.error == QJsonParseError::NoError );
    REQUIRE( doc.isArray() );
    for ( const auto &e : doc.array() )
    {
        const auto o = e.toObject();
        out[o[QStringLiteral( "family" )].toString().toStdString()]
           [o[QStringLiteral( "code" )].toString().toStdString()] =
               o[QStringLiteral( "id" )].toString().toStdString();
    }
    return out;
}

ErrorCodeReport scannedTaxonomies()
{
    ErrorCodeScanner scanner;
    ErrorCodeReport report;
    scanner.scanEnum(
        readFile( std::string( kSourceDir ) +
                  "/src/operators/framework/rs_operator_error.h" ),
        "ErrorCode", report );
    scanner.scanToStringSwitch(
        readFile( std::string( kSourceDir ) +
                  "/src/operators/framework/rs_operator_error.cpp" ),
        "ErrorCode", report );
    scanner.scanHarnessCodes(
        readFile( std::string( kSourceDir ) +
                  "/src/agent/harness/harness_error.h" ), report );
    return report;
}

/// The census query itself — factored so the mutation test can run the same
/// logic against a mutated catalog and prove the red direction.
std::set<std::string> missingCodes( const std::set<std::string> &codes,
                                    const std::map<std::string, std::string> &pages )
{
    std::set<std::string> missing;
    for ( const auto &c : codes )
        if ( !pages.count( c ) )
            missing.insert( c );
    return missing;
}

} // namespace

TEST_CASE( "Error taxonomy: enum ↔ toString switch agree exactly",
           "[contracts9][diagnostics]" )
{
    const auto report = scannedTaxonomies();
    REQUIRE( report.enumValues.size() >= 20 );
    REQUIRE( report.caseMap.size() >= 20 );

    for ( const auto &e : report.enumValues )
    {
        INFO( "enum value missing from toString switch: " << e );
        CHECK( report.caseMap.count( e ) == 1 );
    }
    for ( const auto &[name, str] : report.caseMap )
    {
        INFO( "switch case without enum value: " << name );
        CHECK( report.enumValues.count( name ) == 1 );
        CHECK_FALSE( str.empty() );
    }
    // Idiom sentinels.
    CHECK( report.caseMap.count( "DeviceUnavailable" ) == 1 );
    CHECK( report.caseMap.count( "RuntimeProviderFailed" ) == 1 );
}

TEST_CASE( "Harness codes: constants scanned, runtime taxonomy closed",
           "[contracts9][diagnostics]" )
{
    const auto report = scannedTaxonomies();
    REQUIRE( report.harnessCodes.size() >= 20 );
    CHECK( report.harnessCodes.count( "DatasetNotFound" ) == 1 );

    for ( const auto &[var, code] : report.harnessCodes )
    {
        INFO( "code: " << code );
        const std::string category =
            sicnu::agent::harness::errorCategoryForCode( code );
        CHECK( ( category == "validation" || category == "io" ||
                 category == "runtime" || category == "environment" ) );
        // Unknown codes fall back to Manual — detectable drift for codes the
        // taxonomy claims to know is impossible to assert from outside; the
        // census below pins the catalog side instead.
    }
}

TEST_CASE( "Catalog census: every harness code has a curated page "
           "(allow-listed)",
           "[contracts9][diagnostics][census]" )
{
    const auto report = scannedTaxonomies();
    const auto catalog = catalogByFamily();
    const auto &harnessPages = catalog.at( "harness" );

    std::set<std::string> codes;
    for ( const auto &[var, code] : report.harnessCodes )
        codes.insert( code );
    const auto missing = missingCodes( codes, harnessPages );
    for ( const auto &code : missing )
    {
        INFO( "harness code without curated page: " << code );
        CHECK( allowed( kAllowedHarnessCodesWithoutPage, code ) );
    }
    // Rot guard for the allow-list: an entry whose code exists in the
    // catalog must be removed.
    for ( const auto &e : kAllowedHarnessCodesWithoutPage )
    {
        INFO( "stale allow-list entry: " << e.code );
        CHECK( harnessPages.count( e.code ) == 0 );
    }
}

TEST_CASE( "Catalog census: every operator error string has a curated page "
           "(allow-listed)",
           "[contracts9][diagnostics][census]" )
{
    const auto report = scannedTaxonomies();
    const auto catalog = catalogByFamily();
    const auto &operatorPages = catalog.at( "operator" );

    for ( const auto &[name, str] : report.caseMap )
    {
        if ( name == "Success" || name == "Unknown" )
            continue;
        INFO( "operator code without curated page: " << str );
        CHECK( ( operatorPages.count( str ) == 1 ||
                 allowed( kAllowedOperatorCodesWithoutPage, str ) ) );
    }
    for ( const auto &e : kAllowedOperatorCodesWithoutPage )
        CHECK( operatorPages.count( e.code ) == 0 );
}

TEST_CASE( "Preflight raw string codes are enumerated (stringly surface)",
           "[contracts9][diagnostics][preflight]" )
{
    // The preflight family in diagnostics.json must cover every raw string
    // code emitted by scientific_preflight.cpp addBlocker/addWarning calls.
    const std::string src = readFile(
        std::string( kSourceDir ) +
        "/src/agent/harness/scientific_preflight.cpp" );
    REQUIRE_FALSE( src.empty() );

    std::set<std::string> rawCodes;
    static const std::regex reCall( R"re(\b(addBlocker|addWarning)\s*\()re" );
    // a bare string literal argument: optional quotes around UPPER_SNAKE
    static const std::regex reLit( R"re(^"?\s*([A-Z][A-Z0-9_]*)\s*"?$)re" );
    auto begin = std::sregex_iterator( src.begin(), src.end(), reCall );
    for ( ; begin != std::sregex_iterator(); ++begin )
    {
        const size_t openParen =
            static_cast<size_t>( begin->position() ) + begin->length() - 1;
        // Walk the argument list: arg 2 (index 1) is the code.
        std::vector<std::string> args;
        std::string current;
        int depth = 0;
        for ( size_t i = openParen + 1; i < src.size(); ++i )
        {
            const char c = src[i];
            if ( c == '(' )
                ++depth;
            else if ( c == ')' )
            {
                if ( depth == 0 )
                    break;
                --depth;
            }
            if ( c == ',' && depth == 0 )
            {
                args.push_back( current );
                current.clear();
                continue;
            }
            current.push_back( c );
        }
        args.push_back( current );
        if ( args.size() < 2 )
            continue;
        // trim
        std::string code = args[1];
        const auto isSpace = []( char c ) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        };
        while ( !code.empty() && isSpace( code.front() ) )
            code.erase( code.begin() );
        while ( !code.empty() && isSpace( code.back() ) )
            code.pop_back();
        std::smatch cm;
        if ( std::regex_match( code, cm, reLit ) )
            rawCodes.insert( cm[1].str() );
    }
    REQUIRE( rawCodes.size() >= 2 );

    const auto catalog = catalogByFamily();
    for ( const auto &code : rawCodes )
    {
        INFO( "preflight raw code: " << code );
        // Preflight codes resolve across the two vocabularies it draws
        // from: harness wire codes and its own preflight family.
        CHECK( ( catalog.at( "preflight" ).count( code ) == 1 ||
                 catalog.at( "harness" ).count( code ) == 1 ) );
    }
}

TEST_CASE( "Mutation: a code missing from the catalog is detected",
           "[contracts9][diagnostics][mutation]" )
{
    // Run the same census against a catalog with one page removed: the
    // removed code must be reported. Old (uncensored) behavior — silently
    // absorbing any code — is exactly what this guard forbids.
    const auto catalog = catalogByFamily();
    REQUIRE_FALSE( catalog.empty() );
    const auto &harnessPages = catalog.at( "harness" );
    REQUIRE( harnessPages.size() >= 5 );

    std::set<std::string> codes;
    for ( const auto &[code, pageId] : harnessPages )
        codes.insert( code );
    CHECK( missingCodes( codes, harnessPages ).empty() );

    auto mutated = harnessPages;
    const std::string removed = *codes.begin();
    mutated.erase( removed );
    const auto missing = missingCodes( codes, mutated );
    REQUIRE( missing.size() == 1 );
    CHECK( *missing.begin() == removed );
}
