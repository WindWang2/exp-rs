// src/agent_loop/session_journal.cpp
#include "session_journal.h"

#include "platform/portable.h"

#include <json/reader.h>
#include <json/writer.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace sicnu::agent_loop {
namespace {

std::string jsonToString( const Json::Value &doc )
{
    Json::StreamWriterBuilder builder;
    builder[ "commentStyle" ] = "None";
    builder[ "indentation" ] = "";
    return Json::writeString( builder, doc );
}

bool entryStageValid( const std::string &stage )
{
    return isKnownStageOrTerminal( stage );
}

bool sessionIdCharsetSafe( const std::string &id )
{
    if ( id.empty() || id.size() > 128 )
        return false;
    for ( const unsigned char c : id )
    {
        const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
                        ( c >= '0' && c <= '9' ) || c == '.' || c == '_' || c == '-';
        if ( !ok )
            return false;
    }
    return id != "." && id != "..";
}


bool writeFileAtomic( const fs::path &target, const std::string &body, std::string &error )
{
    // R6: the single sidecar write authority (platform/durable_sidecar.h)
    // owns the whole sequence this function used to re-implement — the
    // atomic_fs #1097 staging claim, the fsync-before-rename durability
    // gate, the single-replace publish (no remove-then-rename window), and
    // the best-effort directory fsync. Same externally-visible contract:
    // old-or-new complete journal, never a torn or missing one.
    const sicnu::platform::sidecar::WriteResult result = sicnu::platform::sidecar::write(
      { sicnu::portable::pathToUtf8( target ), body, "" } );
    if ( !result )
    {
        error = result.error;
        return false;
    }
    return true;
}
Json::Value entryToJson( const JournalEntry &entry )
{
    Json::Value doc( Json::objectValue );
    doc[ "seq" ] = Json::Value( static_cast< Json::Int64 >( entry.seq ) );
    doc[ "at" ] = Json::Value( static_cast< Json::Int64 >( entry.at ) );
    doc[ "stage" ] = entry.stage;
    doc[ "event" ] = entry.event;
    doc[ "payload" ] = entry.payload;
    if ( entry.decision )
        doc[ "decision" ] = entry.decision->toJson();
    return doc;
}

} // namespace

bool SessionJournal::isSafeSessionId( const std::string &sessionId )
{
    return sessionIdCharsetSafe( sessionId );
}

SessionJournal::SessionJournal( std::string sessionId, std::size_t maxEntries )
    : mSessionId( std::move( sessionId ) ), mMaxEntries( maxEntries )
{
}

bool SessionJournal::append( const std::string &event, const std::string &stage,
                             Json::Value payload, long long at,
                             std::optional< DecisionRecord > decision )
{
    if ( event.empty() || !entryStageValid( stage ) )
        return false;
    JournalEntry entry;
    entry.seq = mNextSeq++;
    entry.at = at;
    entry.stage = stage;
    entry.event = event;
    entry.payload = std::move( payload );
    entry.decision = std::move( decision );
    mEntries.push_back( std::move( entry ) );
    while ( mEntries.size() > mMaxEntries )
        mEntries.erase( mEntries.begin() );
    return true;
}

Json::Value SessionJournal::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "schema_version" ] = kSessionJournalSchemaVersion;
    doc[ "session_id" ] = mSessionId;
    doc[ "next_seq" ] = Json::Value( static_cast< Json::Int64 >( mNextSeq ) );
    Json::Value entries( Json::arrayValue );
    for ( const JournalEntry &entry : mEntries )
        entries.append( entryToJson( entry ) );
    doc[ "entries" ] = entries;
    return doc;
}

std::optional< SessionJournal > SessionJournal::fromJson( const Json::Value &doc,
                                                          std::string *error )
{
    auto fail = [ &error ]( const std::string &message ) {
        if ( error )
            *error = message;
        return std::optional< SessionJournal >{};
    };

    if ( !doc.isObject() )
        return fail( "session journal is not an object" );
    if ( !doc.isMember( "schema_version" ) || !doc[ "schema_version" ].isString() )
        return fail( "session journal: missing schema_version" );
    const std::string version = doc[ "schema_version" ].asString();
    if ( version != kSessionJournalSchemaVersion )
        return fail( "session journal: unsupported schema_version '" + version + "'" );

    std::string sessionId;
    if ( !doc.isMember( "session_id" ) || !doc[ "session_id" ].isString() )
        return fail( "session journal: missing session_id" );
    sessionId = doc[ "session_id" ].asString();
    if ( !sessionIdCharsetSafe( sessionId ) )
        return fail( "session journal: unsafe session_id" );

    if ( !doc.isMember( "entries" ) || !doc[ "entries" ].isArray() )
        return fail( "session journal: missing entries array" );

    SessionJournal journal( sessionId );
    long long previousSeq = 0;
    for ( const Json::Value &entryDoc : doc[ "entries" ] )
    {
        if ( !entryDoc.isObject() )
            return fail( "session journal: entry is not an object" );
        JournalEntry entry;
        if ( !entryDoc.isMember( "seq" ) || !entryDoc[ "seq" ].isIntegral() )
            return fail( "session journal: entry missing seq" );
        entry.seq = entryDoc[ "seq" ].asInt64();
        if ( entry.seq <= previousSeq )
            return fail( "session journal: entries are not monotonic" );
        previousSeq = entry.seq;

        if ( !entryDoc.isMember( "at" ) || !entryDoc[ "at" ].isIntegral() )
            return fail( "session journal: entry missing at" );
        entry.at = entryDoc[ "at" ].asInt64();

        if ( !entryDoc.isMember( "stage" ) || !entryDoc[ "stage" ].isString() )
            return fail( "session journal: entry missing stage" );
        entry.stage = entryDoc[ "stage" ].asString();
        if ( !entryStageValid( entry.stage ) )
            return fail( "session journal: unknown stage '" + entry.stage + "'" );

        if ( !entryDoc.isMember( "event" ) || !entryDoc[ "event" ].isString() ||
             entryDoc[ "event" ].asString().empty() )
            return fail( "session journal: entry missing event" );
        entry.event = entryDoc[ "event" ].asString();

        entry.payload = entryDoc.get( "payload", Json::Value( Json::objectValue ) );

        if ( entryDoc.isMember( "decision" ) && !entryDoc[ "decision" ].isNull() )
        {
            std::string decisionError;
            const auto decision = DecisionRecord::fromJson( entryDoc[ "decision" ], &decisionError );
            if ( !decision )
                return fail( "session journal: " + decisionError );
            entry.decision = decision;
        }
        journal.mEntries.push_back( std::move( entry ) );
    }
    journal.mNextSeq = previousSeq + 1;
    return journal;
}

bool SessionJournal::save( const std::string &directory, std::string *error ) const
{
    if ( !sessionIdCharsetSafe( mSessionId ) )
    {
        if ( error )
            *error = "session journal: unsafe session_id '" + mSessionId + "'";
        return false;
    }

    std::error_code ec;
    const fs::path dir = sicnu::portable::pathFromUtf8( directory );
    fs::create_directories( dir, ec );
    if ( ec )
    {
        // The re-check must stay on the error_code overload: the throwing
        // fs::is_directory(status) turned an unrepresentable path (over-long,
        // ENAMETOOLONG) into an uncaught filesystem_error instead of the
        // documented graceful failure. The create error is snapshotted first
        // because a successful probe CLEARS ec — reporting ec.message()
        // after the probe would read "Success" when the directory exists as
        // a non-directory (EEXIST).
        const std::string createMessage = ec.message();
        std::error_code probe;
        if ( !fs::is_directory( dir, probe ) )
        {
            if ( error )
                *error = "cannot create directory " + directory + ": " + createMessage;
            return false;
        }
    }

    // Oversized documents persist through the deterministic compaction
    // projection (decisions preserved, payload bodies dropped).
    Json::Value doc = toJson();
    if ( static_cast< long >( jsonToString( doc ).size() ) > kMaxDocumentBytes )
        doc = compactProjection();

    std::string writeError;
    // The compaction projection above bounds the document in practice; the
    // explicit cap gives the authority a real bound with headroom over the
    // 1 MiB compaction threshold instead of its 32 MiB metadata default.
    const std::size_t kJournalWriteCapBytes = 64ull * 1024ull * 1024ull;
    sicnu::platform::sidecar::WriteRequest journalWrite;
    journalWrite.targetPath = sicnu::portable::pathToUtf8( dir / ( mSessionId + ".json" ) );
    journalWrite.bytes = jsonToString( doc );
    journalWrite.lastGoodSuffix = "";
    journalWrite.maxBytes = kJournalWriteCapBytes;
    const sicnu::platform::sidecar::WriteResult result =
      sicnu::platform::sidecar::write( journalWrite );
    if ( !result )
    {
        if ( error )
            *error = result.error;
        return false;
    }
    return true;
}

std::optional< SessionJournal > SessionJournal::load( const std::string &directory,
                                                      const std::string &sessionId,
                                                      std::string *error )
{
    if ( !sessionIdCharsetSafe( sessionId ) )
    {
        if ( error )
            *error = "session journal: unsafe session_id '" + sessionId + "'";
        return std::nullopt;
    }
    const fs::path path =
      sicnu::portable::pathFromUtf8( directory ) / ( sessionId + ".json" );
    std::error_code ec;
    if ( !fs::exists( path, ec ) )
    {
        if ( error )
            *error = "session journal: not found " + sicnu::portable::pathToUtf8( path );
        return std::nullopt;
    }

    std::ifstream in( path, std::ios::binary );
    if ( !in )
    {
        if ( error )
            *error = "session journal: cannot open " + sicnu::portable::pathToUtf8( path );
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string body = buffer.str();

    Json::CharReaderBuilder builder;
    Json::Value doc;
    std::string parseError;
    const std::unique_ptr< Json::CharReader > reader( builder.newCharReader() );
    if ( !reader->parse( body.data(), body.data() + body.size(), &doc, &parseError ) )
    {
        if ( error )
            *error = "session journal: corrupt document " +
                     sicnu::portable::pathToUtf8( path ) + ": " + parseError;
        return std::nullopt;
    }
    return fromJson( doc, error );
}

Json::Value SessionJournal::compactProjection() const
{
    Json::Value doc = toJson();
    for ( Json::Value &entry : doc[ "entries" ] )
    {
        const long bytes = static_cast< long >( jsonToString( entry[ "payload" ] ).size() );
        Json::Value compacted( Json::objectValue );
        compacted[ "compacted" ] = true;
        compacted[ "original_bytes" ] = Json::Value( static_cast< Json::Int64 >( bytes ) );
        entry[ "payload" ] = compacted;
    }
    return doc;
}

SessionJournal::ReplayResult SessionJournal::replay() const
{
    ReplayResult result;
    result.entries = mEntries;
    for ( const JournalEntry &entry : mEntries )
    {
        if ( entry.event == "stage_enter" && isKnownStage( entry.stage ) )
        {
            result.finalStage = entry.stage;
            if ( entry.stage == stages::kReplan )
                ++result.replanCount;
        }
        else if ( entry.event == "terminal" && isKnownTerminalState( entry.stage ) )
        {
            result.terminalState = entry.stage;
            if ( entry.payload.isString() )
                result.stopReason = entry.payload.asString();
        }
    }
    return result;
}

} // namespace sicnu::agent_loop
