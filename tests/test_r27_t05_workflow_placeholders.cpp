// tests/test_r27_t05_workflow_placeholders.cpp — #1775 (WE-14) + #1776
// (WE-15) + #1780 (WE-21, detector half).
//
// #1775: an unknown or case-mismatched named port must NOT silently resolve
// to the canonical output path; only the declared "output" port keeps that
// fallback.
// #1776: workflow env expansion is allow-listed and secret-shaped names are
// never expanded; arbitrary ${env.X}/${UPPER} no longer reads host env into
// resolved params/provenance.
// #1780: the shared detector behind the TaskCenter pre-launch gate reports
// leftover step/task placeholders (env refs excluded).
#include <catch2/catch_test_macros.hpp>

#include "workflow/placeholder_grammar.h"

#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <cstdlib>
static void portableSetenv( const char *key, const char *value )
{
    _putenv( ( std::string( key ) + "=" + value ).c_str() );
}
static void portableUnsetenv( const char *key )
{
    _putenv( ( std::string( key ) + "=" ).c_str() );
}
#else
static void portableSetenv( const char *key, const char *value )
{
    setenv( key, value, 1 );
}
static void portableUnsetenv( const char *key )
{
    unsetenv( key );
}
#endif

using namespace sicnu::workflow;

namespace
{

Json::Value payloadWithPorts()
{
    Json::Value payload( Json::objectValue );
    payload["masked_path"] = "/tmp/out/masked.tif";
    payload["output"] = "/tmp/out/primary.tif";
    return payload;
}

} // namespace

TEST_CASE( "#1775 named ports resolve exactly or not at all",
           "[workflow][r27t05][issue-1775]" )
{
    const Json::Value payload = payloadWithPorts();
    const std::string canonical = "/detected/primary.tif";

    // Exact named port hit.
    CHECK( resolvePlaceholderPort( payload, canonical, "masked_path" ) == "/tmp/out/masked.tif" );
    // Unknown named port: NO silent fallback to the canonical output.
    CHECK( resolvePlaceholderPort( payload, canonical, "threshold_map" ).empty() );
    // Case-mismatched port: no case-insensitive rescue either.
    CHECK( resolvePlaceholderPort( payload, canonical, "Masked_Path" ).empty() );
    CHECK( resolvePlaceholderPort( payload, canonical, "MASKED_PATH" ).empty() );
    // The canonical "output" port: the payload's own "output" member wins
    // (exact-key rule); the canonical path only backs it up when absent.
    CHECK( resolvePlaceholderPort( payload, canonical, "output" ) == "/tmp/out/primary.tif" );
    Json::Value noOutputPayload = payloadWithPorts();
    noOutputPayload.removeMember( "output" );
    CHECK( resolvePlaceholderPort( noOutputPayload, canonical, "output" ) == canonical );
}

TEST_CASE( "#1776 env expansion is allow-listed and secret-shaped names never expand",
           "[workflow][r27t05][issue-1776]" )
{
    portableSetenv( "SICNU_R27T05_PROBE", "/lab/data" );
    portableSetenv( "R27T05_SECRET_PROBE", "hunter2" );
    portableSetenv( "SICNU_R27T05_TOKEN", "hunter2" );
    portableSetenv( "HOME", "/home/kevin" );

    const auto identity = []( const PlaceholderRef & ) -> std::string { return std::string(); };

    // Allow-listed project-namespace name expands.
    CHECK( substitutePlaceholders( "data at ${env.SICNU_R27T05_PROBE}", identity )
           == "data at /lab/data" );
    // Allow-listed shipped convention name expands (WORK).
    portableSetenv( "WORK", "/tmp/r27t05work" );
    CHECK( substitutePlaceholders( "out: ${WORK}/ndvi.tif", identity ) == "out: /tmp/r27t05work/ndvi.tif" );
    portableUnsetenv( "WORK" );

    // Arbitrary host env does NOT expand — the raw ref stays visible.
    CHECK( substitutePlaceholders( "home is ${HOME}", identity ) == "home is ${HOME}" );
    CHECK( substitutePlaceholders( "leak ${env.PATH}", identity ) == "leak ${env.PATH}" );
    CHECK( substitutePlaceholders( "leak ${R27T05_SECRET_PROBE}", identity )
           == "leak ${R27T05_SECRET_PROBE}" );
    // Secret-shaped name is refused even under an allow-listed prefix.
    CHECK( substitutePlaceholders( "key ${SICNU_R27T05_TOKEN}", identity )
           == "key ${SICNU_R27T05_TOKEN}" );

    CHECK( envNameLooksSecret( "SICNU_API_KEY" ) );
    CHECK( envNameLooksSecret( "MY_PASSWORD" ) );
    CHECK_FALSE( envNameLooksSecret( "SICNU_LAB_DATA_DIR" ) );
    CHECK( isEnvExpansionAllowed( "SICNU_R27T05_PROBE" ) );
    CHECK_FALSE( isEnvExpansionAllowed( "SICNU_API_KEY" ) );
    CHECK_FALSE( isEnvExpansionAllowed( "HOME" ) );

    portableUnsetenv( "SICNU_R27T05_PROBE" );
    portableUnsetenv( "R27T05_SECRET_PROBE" );
    portableUnsetenv( "SICNU_R27T05_TOKEN" );
}

TEST_CASE( "#1780 the gate detector reports leftover step/task placeholders",
           "[workflow][r27t05][issue-1780]" )
{
    CHECK( containsUnresolvedStepPlaceholder( "$step1.masked_path" ) );
    CHECK( containsUnresolvedStepPlaceholder( "prefix ${task.parent.output} suffix" ) );
    CHECK( containsUnresolvedStepPlaceholder( "${task.12.output}" ) );
    // Env refs are not workflow data — the gate must not fire on them.
    CHECK_FALSE( containsUnresolvedStepPlaceholder( "${HOME}" ) );
    CHECK_FALSE( containsUnresolvedStepPlaceholder( "${env.PATH}" ) );
    // Fully substituted text and plain text are clean.
    CHECK_FALSE( containsUnresolvedStepPlaceholder( "/tmp/out/masked.tif" ) );
    CHECK_FALSE( containsUnresolvedStepPlaceholder( "cost 5$ exactly" ) );
}
