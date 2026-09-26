/***************************************************************************
 * test_data_parse_integrity_12.cpp — Platform 12.0 · Oracle O-11 (extended)
 *
 * WHOLE-CORPUS PARSE GATE for every shipped `data/**\/*.json` file.
 *
 * WHY THIS EXISTS (evidence-anchored, see EVIDENCE.md E-16):
 *
 *   E-15 established that ONE shipped corpus (data/help/commands.json) had
 *   stopped parsing while the byte-gated snapshot stayed green. Asking the
 *   obvious follow-up — "how many files are like this?" — gives 25 of 609:
 *
 *     E-16 Family A · 21 files whose object structure was re-opened on top of
 *       itself (a key line emitted twice, once bare and once with a trailing
 *       space), so the parser dies at the first duplicated level:
 *         E-15      data/help/commands.json
 *         E-16      data/agent/capabilities/preprocess.json
 *         E-16      19 x data/processing/algorithm_meta/capability/rs-*.json
 *
 *     E-16 Family B ·  4 files truncated to ZERO BYTES:
 *         data/processing/algorithm_meta/rs-temporal-extract-regions.json
 *         data/processing/algorithm_meta/rs-temporal-harmonic-breaks.json
 *         data/processing/algorithm_meta/rs-temporal-region-features.json
 *         data/processing/algorithm_meta/rs-temporal-regularize.json
 *
 *   Both families were introduced by MERGE COMMITS, not by authored edits:
 *
 *     43dcf19cd  merge: land PR #1022 (terrain-hydrology-11)
 *                -> 19 files OK -> FAIL; the merge diff against EACH parent is
 *                   insert-only, so the breakage exists in neither parent
 *                   ("evil merge").
 *     4713528ef  merge: land PR #1021 (spectral-intelligence-11)
 *                ->  4 files non-empty -> 0 bytes.
 *
 *   WHY NO EXISTING GATE CAUGHT IT. Each affected corpus already had a test,
 *   and each test was true and vacuous with respect to the defect:
 *
 *     - test_capability_drift.cpp:63 asserts loadProblems().empty() — but
 *       points CapabilityKnowledge at data/agent/capabilities, a DIFFERENT
 *       corpus from data/processing/algorithm_meta/capability.
 *     - AlgorithmMetaStore::loadFromDirectory (src/processing/framework/
 *       algorithm_meta_store.cpp) hits the same 24 files and does a bare
 *       `continue`, recording nothing at all.
 *     - The contract snapshot compares recorded-vs-recorded, which by
 *       construction cannot notice that the live corpus stopped parsing.
 *
 *   So this lane asserts the property every consumer assumes and no gate
 *   checked: **every shipped data file parses.**
 *
 * DESIGN NOTES
 *  - It walks `data/` from CMAKE_SOURCE_DIR, i.e. the SOURCE tree. A bad edit
 *    fails here before it can reach any compiled resource bundle.
 *  - It uses ONLY CMAKE_SOURCE_DIR and a stock JSON parser — no repo loader.
 *    Routing through a tolerant loader is precisely what hid E-15.
 *  - Failures are reported per file with the parser's own message, so a red
 *    run names the file and the offset instead of a bare boolean.
 *  - Non-vacuity floors are asserted on the file count and on the count of
 *    files actually parsed, so a check that silently stops walking cannot
 *    pass.
 *  - SCOPE: this lane does NOT repair the 25 files and does NOT assert their
 *    content. Repair is filed as a P0 issue (E-16f) because it spans three
 *    corpora and two ownership boundaries outside this track. What this track
 *    owns, and what this lane delivers, is the GATE.
 *
 *   NOT-ORPHANED: the allowlist below is a deliberate, loud, shrinking list.
 *   It exists so this gate can land and be proven potent *before* the repair,
 *   and `CHECK( allowlist.size() <= kMaxTolerated )` guarantees that repairing
 *   files can only ever tighten it. Every entry names its defect family, so
 *   the list cannot rot into a blanket skip.
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringList>

#include <json/json.h>

#include <memory>
#include <string>

namespace {

const char *const kDataDir = "/data";

/// Files known to be unparseable at the time this gate was authored.
///
/// This is a TO-DO list, not a permission slip. Each entry is a shipped file
/// with a filed P0 issue; the gate is what keeps the list from growing, and
/// the ceiling assertion below is what forces it to shrink.
///
/// Format: <path relative to data/>  =>  <defect family>
struct KnownBadFile
{
    const char *path;
    const char *family;
};

const KnownBadFile *knownBadFiles( std::size_t &count )
{
    static const KnownBadFile kKnown[] = {
        // --- Family A: duplicated-key prefix block (evil merge 43dcf19cd) ---
        // NOTE: help/commands.json (E-15) and agent/capabilities/preprocess.json
        // were repaired earlier in this track by restoring the authored
        // revision, so neither is listed here. The gate's own "stale
        // allowlist" assertion is what forced their removal; see E-16g.
        //
        // PRUNED (closure-io-processing-r4): all remaining 19 Family A
        // capability sidecars now parse — the sidecar regeneration on master
        // (capability-search #1140 lineage) rewrote them as valid JSON. The
        // gate's stale-allowlist assertion went red with the full list, and
        // this is the mandated prune: a repaired file may not linger on the
        // list (a list that outlives its defect starts hiding new ones).

        // --- Family B: truncated to zero bytes (evil merge 4713528ef) ---
        { "processing/algorithm_meta/rs-temporal-extract-regions.json", "B:zero-byte" },
        { "processing/algorithm_meta/rs-temporal-harmonic-breaks.json", "B:zero-byte" },
        { "processing/algorithm_meta/rs-temporal-region-features.json", "B:zero-byte" },
        { "processing/algorithm_meta/rs-temporal-regularize.json", "B:zero-byte" },
    };
    count = sizeof( kKnown ) / sizeof( kKnown[0] );
    return kKnown;
}

/// The ceiling. Repairing a file cannot break this assertion; ADDING one does.
/// Set to the authored defect count so the gate is exactly as tight as the
/// evidence supports at the moment it lands. Lowered 25 -> 23 when the two
/// already-repaired Family-A files were pruned (E-16g); lowered 23 -> 4 when
/// the whole regenerated Family A parsed and was pruned
/// (closure-io-processing-r4) — the four zero-byte Family-B files remain.
constexpr std::size_t kMaxTolerated = 4;

QString dataRoot()
{
    return QString::fromStdString( std::string( CMAKE_SOURCE_DIR ) + kDataDir );
}

struct ParseOutcome
{
    bool parsed = false;
    std::string error;
};

ParseOutcome parseFile( const QString &path )
{
    ParseOutcome outcome;

    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
    {
        outcome.error = "unreadable";
        return outcome;
    }
    const QByteArray bytes = f.readAll();

    if ( bytes.isEmpty() )
    {
        // Zero bytes is not "invalid JSON" in a parser's eyes so much as an
        // empty document; treat it explicitly so the message is actionable.
        outcome.error = "file is empty (0 bytes)";
        return outcome;
    }

    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string parseError;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    outcome.parsed = reader->parse( bytes.constData(),
                                    bytes.constData() + bytes.size(), &doc,
                                    &parseError );
    outcome.error = parseError;
    return outcome;
}

} // namespace

TEST_CASE( "every shipped data JSON file parses, apart from the filed P0 set",
           "[dataparse12][o11][e16]" )
{
    const QString root = dataRoot();
    INFO( "data dir: " << root.toStdString() );
    REQUIRE( QFileInfo( root ).isDir() );

    // --- enumerate every *.json under data/ -------------------------------
    QStringList files;
    QDirIterator it( root, { QStringLiteral( "*.json" ) }, QDir::Files,
                     QDirIterator::Subdirectories );
    while ( it.hasNext() )
    {
        const QString abs = it.next();
        // Relative to data/, with forward slashes, so the allowlist is stable
        // across platforms.
        files << QDir( root ).relativeFilePath( abs );
    }
    files.sort();

    INFO( "json files under data/: " << files.size() );
    // Non-vacuity: this corpus is large. If the walk silently stops, fail.
    REQUIRE( files.size() >= 400 );

    std::size_t knownCount = 0;
    const KnownBadFile *const known = knownBadFiles( knownCount );
    QSet<QString> allowed;
    for ( std::size_t i = 0; i < knownCount; ++i )
        allowed.insert( QString::fromUtf8( known[i].path ) );

    // A stale allowlist entry (file repaired, or file gone) must be visible:
    // otherwise the list outlives the defect and starts hiding new ones.
    for ( std::size_t i = 0; i < knownCount; ++i )
    {
        const KnownBadFile &entry = known[i];
        const QString rel = QString::fromUtf8( entry.path );
        const QFileInfo info( QDir( root ).filePath( rel ) );
        if ( !info.exists() )
        {
            UNSCOPED_INFO( "allowlisted file no longer exists: "
                           << rel.toStdString() );
        }
    }

    int parsed = 0;
    int tolerated = 0;
    QStringList unexpected;
    QStringList repairedButStillListed;

    for ( const QString &rel : files )
    {
        const ParseOutcome outcome = parseFile( QDir( root ).filePath( rel ) );

        if ( outcome.parsed )
        {
            ++parsed;
            if ( allowed.contains( rel ) )
            {
                // The file now parses although it is still on the allowlist.
                // That is good news that must not go silent — the list has to
                // be pruned, so report it and let the gate go red until it is.
                repairedButStillListed
                    << QStringLiteral( "%1 now parses but is still allowlisted" )
                           .arg( rel );
            }
            continue;
        }

        if ( allowed.contains( rel ) )
        {
            ++tolerated;
            continue;
        }

        unexpected << QStringLiteral( "%1 does not parse: %2" )
                          .arg( rel, QString::fromStdString( outcome.error ) );
    }

    // --- report before asserting, so a red run is actionable ---------------
    UNSCOPED_INFO( "parsed OK: " << parsed << " / " << files.size() );
    UNSCOPED_INFO( "tolerated (filed P0): " << tolerated );
    for ( const QString &u : unexpected )
        UNSCOPED_INFO( "NEW unparseable data file: " << u.toStdString() );
    for ( const QString &r : repairedButStillListed )
        UNSCOPED_INFO( "stale allowlist entry: " << r.toStdString() );

    // 1. No file outside the filed set may fail to parse. This is the gate.
    CHECK( unexpected.isEmpty() );

    // 2. The allowlist may not grow. Repair shrinks it; a new defect cannot
    //    hide behind it.
    CHECK( allowed.size() <= kMaxTolerated );

    // 3. Every allowlisted file must still be a real, unparseable file, so a
    //    repaired file forces the list to be pruned rather than lingering.
    CHECK( repairedButStillListed.isEmpty() );

    // 4. Non-vacuity: we actually parsed the corpus.
    CHECK( parsed >= 400 );
}
