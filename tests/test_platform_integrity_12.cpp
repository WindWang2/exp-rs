/***************************************************************************
 * test_platform_integrity_12.cpp — Platform 12.0 · Oracle O-9
 *
 * Registry load problems must be EMPTY, and loaded coverage must be
 * NON-VACUOUS. This is the gate that 11.0 lacked.
 *
 * WHY THIS EXISTS (evidence-anchored, see EVIDENCE.md E-5):
 *
 *   data/agent/capabilities/preprocess.json shipped on master (commit
 *   eea3aee65, PR #1024) with a malformed final object — a missing comma
 *   followed by a duplicated `intents`/`resource`/`limitations` triplet.
 *   capability_knowledge.cpp:163 fails to parse the file, pushes a string
 *   into mLoadProblems and `continue`s — so the ENTIRE preprocess family's
 *   capability knowledge (rs:modis_georeference, gdal:orthorectification,
 *   and every sibling in the file) was silently dropped at runtime.
 *
 *   Nothing noticed. `grep -rn loadProblems src tests` showed the four
 *   cartography registries surface theirs through `cartography:preflight`,
 *   but CapabilityKnowledge::loadProblems() had ZERO consumers. A whole
 *   registry could be unparseable and every suite stayed green.
 *
 * That is a false-success surface — the exact class the 12.0 mandate exists
 * to close. The loader was already honest (it recorded the problem); the
 * platform simply never asked.
 *
 * DESIGN NOTES
 *  - This is an INTEGRITY gate, not a business test: it reads the real
 *    repository data directory, so it fails today on master and protects
 *    every future edit to data/agent/capabilities/**.
 *  - It must not become a skip when the directory is missing: a missing
 *    capabilities directory means "broken install", which is exactly what
 *    the loader's own comment at capability_knowledge.cpp:141 warns about.
 *    A skip there would be a false green.
 *  - The floors are deliberately generous lower bounds (the surface may
 *    only grow); they exist to stop a silently-empty load from passing.
 ***************************************************************************/
#include "agent/harness/capability_knowledge.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <string>
#include <vector>

namespace {

std::string sourceRoot()
{
    return std::string( CMAKE_SOURCE_DIR );
}

/// The repository's authored capability directory. Read from the source tree
/// (not the install tree) so the gate tests what is committed.
std::string repoCapabilitiesDir()
{
    return sourceRoot() + "/data/agent/capabilities";
}

} // namespace

using sicnu::agent::harness::CapabilityKnowledge;

TEST_CASE( "capability knowledge directory exists in the source tree",
           "[integrity12]" )
{
    const QString dir = QString::fromStdString( repoCapabilitiesDir() );
    INFO( "capabilities dir: " << repoCapabilitiesDir() );
    REQUIRE( QFileInfo( dir ).isDir() );

    // Non-vacuous: the repository ships one file per capability family.
    const QDir d( dir );
    const auto jsonFiles =
        d.entryList( { QStringLiteral( "*.json" ) }, QDir::Files );
    INFO( "json files found: " << jsonFiles.size() );
    CHECK( jsonFiles.size() >= 10 );
}

TEST_CASE( "capability knowledge loads with ZERO problems",
           "[integrity12][o9]" )
{
    auto &knowledge = CapabilityKnowledge::instance();
    knowledge.setDirectory( repoCapabilitiesDir() );
    const int loaded = knowledge.reload();

    // Print every problem before asserting, so a failure is actionable
    // rather than a bare count.
    const std::vector<std::string> problems = knowledge.loadProblems();
    for ( const std::string &problem : problems )
        UNSCOPED_INFO( "load problem: " << problem );

    // THE GATE. A registry that cannot be read completely is a defect, not
    // a warning. This is what the missing consumer allowed to go unnoticed.
    CHECK( problems.empty() );

    // Non-vacuous coverage: a silently-empty load must not pass.
    INFO( "entries loaded: " << loaded );
    CHECK( loaded >= 100 );

    // The loader and its own accessor must agree — guards against a future
    // refactor that returns one count from reload() and another from ids().
    const std::vector<std::string> ids = knowledge.entryIds();
    INFO( "entry ids reported: " << ids.size() );
    CHECK( ids.size() == static_cast<std::size_t>( loaded ) );
}

TEST_CASE( "the specific entry that was corrupted on master is loadable",
           "[integrity12][o9]" )
{
    // Regression pin for E-5. `gdal:orthorectification` was the tail entry of
    // the corrupted preprocess.json; because the loader drops the whole file
    // on a parse error, this entry AND its 20 siblings vanished together.
    // Asserting on the entry (not just the problem list) means the gate still
    // catches the corruption if the loader is ever changed to report parse
    // errors by a different mechanism.
    auto &knowledge = CapabilityKnowledge::instance();
    knowledge.setDirectory( repoCapabilitiesDir() );
    knowledge.reload();

    const std::vector<std::string> ids = knowledge.entryIds();
    const auto has = [&ids]( const std::string &needle ) {
        return std::find( ids.begin(), ids.end(), needle ) != ids.end();
    };

    INFO( "gdal:orthorectification present: " << has( "gdal:orthorectification" ) );
    CHECK( has( "gdal:orthorectification" ) );
    CHECK( has( "rs:modis_georeference" ) );

    // Family membership proves the whole file was parsed, not just a fragment.
    const std::vector<std::string> preprocess =
        knowledge.entryIdsForSurface( "operator" );
    CHECK( preprocess.size() >= 100 );
}

TEST_CASE( "reloading the same directory is idempotent",
           "[integrity12][o9]" )
{
    // A registry that accumulates problems across reloads would produce
    // phantom failures on the second run of the same suite — and the mandate
    // requires key gates to pass TWICE consecutively.
    auto &knowledge = CapabilityKnowledge::instance();
    knowledge.setDirectory( repoCapabilitiesDir() );

    const int first = knowledge.reload();
    const std::size_t firstProblems = knowledge.loadProblems().size();
    const std::size_t firstIds = knowledge.entryIds().size();

    const int second = knowledge.reload();
    const std::size_t secondProblems = knowledge.loadProblems().size();
    const std::size_t secondIds = knowledge.entryIds().size();

    INFO( "first: " << first << " entries, " << firstProblems << " problems" );
    INFO( "second: " << second << " entries, " << secondProblems << " problems" );
    CHECK( first == second );
    CHECK( firstProblems == 0 );
    CHECK( secondProblems == 0 );
    CHECK( firstIds == secondIds );
}
