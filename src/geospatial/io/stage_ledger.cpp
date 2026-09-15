/***************************************************************************
  geospatial/io/stage_ledger.cpp
  Geospatial I/O, COG & Interchange 11.0 — dataset-level staging ledger,
  attach-existing and orphan sweep.
 ***************************************************************************/

#include "geospatial/io/stage_ledger.h"

#include "geospatial/io/param_guard.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/util/time_normalization.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <cpl_error.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>
#include <fstream>
#include <json/reader.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace sicnu::geo::io
{

const char *const kStageLedgerSuffix = ".sicnu-stage-ledger.json";

namespace
{

constexpr int kLedgerSchemaVersion = 1;

/// A ledger is a small JSON sidecar; anything bigger is planted (F8 guard).
constexpr std::uintmax_t kMaxLedgerBytes = 16ull * 1024ull * 1024ull;

std::int64_t nowEpochNanos()
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::nanoseconds>( now ).count();
}

void writeLedger( const StageRecord &record )
{
  const Json::Value json = stageRecordToJson( record );
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  const std::string text = Json::writeString( builder, json );
  atomic_fs::writeFileAtomic( stageLedgerPath( record.finalPath ), [ & ]( const std::string &stagedPath ) {
    std::ofstream file( stagedPath, std::ios::binary | std::ios::trunc );
    if ( !file )
      throw GeoError( ErrorCode::IoError, "stage ledger: cannot create " + stagedPath );
    file.write( text.data(), static_cast<std::streamsize>( text.size() ) );
    file.flush();
    if ( !file )
      throw GeoError( ErrorCode::IoError, "stage ledger: write failed for " + stagedPath );
  } );
}

/// The staged-name shape produced by atomic_fs::stagedPathFor():
/// "<stem>.<digits>.<digits>.tmp<extension>".
bool isStagedShapedName( const std::string &name )
{
  const std::size_t tmp = name.find( ".tmp" );
  if ( tmp == std::string::npos )
    return false;
  const std::string remainder = name.substr( tmp + 4 );
  if ( !remainder.empty() && remainder.front() != '.' )
    return false; // ".temporary" style names are not ours
  // Walk back over two dot-separated all-digit segments.
  std::size_t end = tmp;
  for ( int segment = 0; segment < 2; ++segment )
  {
    const std::size_t start = name.rfind( '.', end - 1 );
    if ( start == std::string::npos || start + 1 >= end )
      return false;
    for ( std::size_t i = start + 1; i < end; ++i )
    {
      if ( name[i] < '0' || name[i] > '9' )
        return false;
    }
    end = start;
  }
  return true;
}

bool endsWith( const std::string &text, const char *suffix )
{
  const std::size_t length = std::strlen( suffix );
  return text.size() >= length && text.compare( text.size() - length, length, suffix ) == 0;
}

AttachCheck attachCheckFrom( const StageRecord &record, const std::vector<AttachIssue> &issues )
{
  AttachCheck check;
  check.attachable = issues.empty();
  check.record = record;
  check.issues = issues;
  return check;
}

} // namespace

Json::Value stageRecordToJson( const StageRecord &record )
{
  Json::Value json;
  json["schema_version"] = kLedgerSchemaVersion;
  json["run_id"] = record.runId;
  json["producer"] = record.producer;
  json["final_path"] = record.finalPath;
  json["staged_path"] = record.stagedPath;
  json["driver"] = record.driver;
  Json::Value shape;
  shape["width"] = record.width;
  shape["height"] = record.height;
  shape["band_count"] = record.bandCount;
  json["declared_shape"] = shape;
  json["state"] = record.state;
  json["updated_utc"] = record.updatedAtUtc;
  return json;
}

StageRecord stageRecordFromJson( const Json::Value &json )
{
  if ( !json.isObject() )
    throw GeoError( ErrorCode::InvalidMetadata, "stage ledger entry is not a JSON object" );
  if ( !json["schema_version"].isInt() )
    throw GeoError( ErrorCode::InvalidMetadata, "stage ledger schema_version has a foreign type" );
  if ( !json["run_id"].isString() || !json["final_path"].isString() || !json["staged_path"].isString() )
    throw GeoError( ErrorCode::InvalidMetadata, "stage ledger identity fields have foreign types" );
  if ( json["schema_version"].asInt() != kLedgerSchemaVersion )
  {
    Json::Value details;
    details["found_version"] = json["schema_version"].asInt();
    details["expected_version"] = kLedgerSchemaVersion;
    throw GeoError( ErrorCode::InvalidMetadata, "stage ledger schema version mismatch", details );
  }
  StageRecord record;
  record.runId = json["run_id"].asString();
  record.producer = json["producer"].asString();
  record.finalPath = json["final_path"].asString();
  record.stagedPath = json["staged_path"].asString();
  record.driver = json["driver"].asString();
  record.width = json["declared_shape"]["width"].asInt();
  record.height = json["declared_shape"]["height"].asInt();
  record.bandCount = json["declared_shape"]["band_count"].asInt();
  record.state = json["state"].asString();
  record.updatedAtUtc = json["updated_utc"].asString();
  static const char *kKnownStates[] = { "staged", "finalized", "discarded" };
  const bool stateKnown = std::any_of( std::begin( kKnownStates ), std::end( kKnownStates ),
                                       [ & ]( const char *state ) { return record.state == state; } );
  if ( !stateKnown )
  {
    Json::Value details;
    details["state"] = record.state;
    throw GeoError( ErrorCode::InvalidMetadata, "stage ledger state is unknown", details );
  }
  return record;
}

std::string stageLedgerPath( const std::string &finalPath )
{
  return finalPath + kStageLedgerSuffix;
}

void recordStaged( const StageRecord &record )
{
  if ( record.runId.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "stage ledger: run_id is required" );
  if ( record.finalPath.empty() || record.stagedPath.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "stage ledger: final and staged paths are required" );
  // Invariant (review F7): staging lives NEXT TO the target — the atomic
  // publish is a same-directory rename, and a foreign-directory "staged"
  // path would let a journal rename an arbitrary file onto the target.
  {
    const fs::path finalParent = fs::u8path( record.finalPath ).parent_path().lexically_normal();
    const fs::path stagedParent = fs::u8path( record.stagedPath ).parent_path().lexically_normal();
    if ( finalParent != stagedParent )
      throw GeoError( ErrorCode::InvalidArgument,
                      "stage ledger: staged path must live in the target's directory" );
  }
  StageRecord journaled = record;
  journaled.state = "staged";
  journaled.updatedAtUtc = instantToUtcString( nowEpochNanos() );
  writeLedger( journaled );
}

StageRecord readStageLedger( const std::string &finalPath )
{
  const std::string ledgerPath = stageLedgerPath( finalPath );
  if ( !atomic_fs::fileExists( ledgerPath ) )
    throw GeoError( ErrorCode::NotFound, "stage ledger missing for " + finalPath );
  if ( atomic_fs::fileSize( ledgerPath ) > kMaxLedgerBytes )
    throw GeoError( ErrorCode::InvalidMetadata, "stage ledger exceeds the size cap; refusing to read" );
  std::ifstream file( ledgerPath, std::ios::binary );
  if ( !file )
    throw GeoError( ErrorCode::IoError, "stage ledger: cannot open " + ledgerPath );
  std::string text( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
  Json::Value json;
  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string error;
  if ( !reader->parse( text.data(), text.data() + text.size(), &json, &error ) )
  {
    Json::Value details;
    details["parse_error"] = error;
    throw GeoError( ErrorCode::InvalidMetadata, "stage ledger is not valid JSON", details );
  }
  return stageRecordFromJson( json );
}

void removeStageLedger( const std::string &finalPath )
{
  atomic_fs::removeFileQuiet( stageLedgerPath( finalPath ) );
}

Json::Value AttachCheck::toJson() const
{
  Json::Value json;
  json["attachable"] = attachable;
  json["record"] = stageRecordToJson( record );
  Json::Value issueArray( Json::arrayValue );
  for ( const AttachIssue &issue : issues )
  {
    Json::Value entry;
    entry["code"] = issue.code;
    entry["message"] = issue.message;
    issueArray.append( entry );
  }
  json["issues"] = issueArray;
  return json;
}

AttachCheck attachExisting( const std::string &finalPath )
{
  StageRecord record;
  std::vector<AttachIssue> issues;
  try
  {
    record = readStageLedger( finalPath );
  }
  catch ( const GeoError &error )
  {
    const bool missing = error.code() == ErrorCode::NotFound;
    issues.push_back( AttachIssue{ missing ? "ledger_missing" : "ledger_invalid", error.what() } );
    return attachCheckFrom( record, issues );
  }
  catch ( const std::exception & )
  {
    issues.push_back( AttachIssue{ "ledger_invalid", "stage ledger is malformed (foreign JSON types)" } );
    return attachCheckFrom( record, issues );
  }

  if ( record.state != "staged" )
  {
    issues.push_back( AttachIssue{ "state_not_staged", "ledger state is '" + record.state + "'"
                                                                       "; nothing to attach" } );
    return attachCheckFrom( record, issues );
  }
  if ( !atomic_fs::fileExists( record.stagedPath ) )
  {
    issues.push_back( AttachIssue{ "staged_missing", "staged dataset is gone: " + record.stagedPath } );
    return attachCheckFrom( record, issues );
  }

  // The staged dataset must still open, and what opens must be what was
  // declared. A renamed/truncated/spoofed staged file fails here, which is
  // the point: attach is a trust boundary for the resume path.
  CPLErrorStateBackuper errorBackuper( CPLQuietErrorHandler );
  GDALDataset *dataset =
    GDALDataset::Open( record.stagedPath.c_str(), GDAL_OF_READONLY | GDAL_OF_RASTER | GDAL_OF_VECTOR );
  if ( !dataset )
  {
    const char *lastError = CPLGetLastErrorMsg();
    issues.push_back( AttachIssue{ "staged_unopenable", lastError && *lastError
                                                         ? lastError
                                                         : "staged dataset failed to open in GDAL" } );
    return attachCheckFrom( record, issues );
  }
  const std::string openedDriver = dataset->GetDriver() ? dataset->GetDriver()->GetDescription() : std::string();
  const int openedWidth = dataset->GetRasterXSize();
  const int openedHeight = dataset->GetRasterYSize();
  const int openedBands = dataset->GetRasterCount();
  GDALClose( dataset );

  if ( !record.driver.empty() && record.driver != openedDriver )
  {
    Json::Value details;
    details["declared"] = record.driver;
    details["opened"] = openedDriver;
    issues.push_back( AttachIssue{ "driver_mismatch", "staged dataset driver differs from the ledger declaration" } );
  }
  if ( record.width > 0 || record.height > 0 || record.bandCount > 0 )
  {
    if ( record.width != openedWidth || record.height != openedHeight || record.bandCount != openedBands )
    {
      Json::Value details;
      details["declared_width"] = record.width;
      details["opened_width"] = openedWidth;
      issues.push_back( AttachIssue{ "shape_mismatch", "staged dataset shape differs from the ledger declaration" } );
    }
  }
  return attachCheckFrom( record, issues );
}

void finalizeAttached( const std::string &finalPath, const FinalizeManifestFields *manifest )
{
  const AttachCheck check = attachExisting( finalPath );
  if ( !check.attachable )
  {
    Json::Value details;
    Json::Value issueArray( Json::arrayValue );
    for ( const AttachIssue &issue : check.issues )
    {
      Json::Value entry;
      entry["code"] = issue.code;
      entry["message"] = issue.message;
      issueArray.append( entry );
    }
    details["issues"] = issueArray;
    throw GeoError( ErrorCode::InvalidArgument, "attach validation failed; transaction cannot be finalized", details );
  }
  const StageRecord record = check.record;

  if ( manifest )
  {
    // Digest + manifest bind to the STAGED bytes; the rename to the final
    // name does not change content. Writing the manifest as a staged sidecar
    // keeps it inside the group transaction (sidecars publish first).
    Json::Value manifestJson = buildFinalizeManifest( record.stagedPath, *manifest );
    // The provenance path must name the FINAL dataset, not the staging name.
    manifestJson["path_display"] = ResourceUri::parse( record.finalPath ).display();
    writeFinalizeManifest( record.stagedPath, manifestJson );
  }

  atomic_fs::fsyncFile( record.stagedPath );
  atomic_fs::publishStagedGroup( record.stagedPath, record.finalPath );

  StageRecord finalized = record;
  finalized.state = "finalized";
  finalized.updatedAtUtc = instantToUtcString( nowEpochNanos() );
  writeLedger( finalized );
}

void discardAttached( const std::string &finalPath )
{
  StageRecord record;
  try
  {
    record = readStageLedger( finalPath );
  }
  catch ( const GeoError & )
  {
    return; // nothing journaled → nothing to discard
  }
  if ( record.state != "staged" )
  {
    Json::Value details;
    details["state"] = record.state;
    throw GeoError( ErrorCode::InvalidArgument, "stage ledger: refusing to discard a non-open transaction", details );
  }
  atomic_fs::discardStaged( record.stagedPath );
  removeStageLedger( finalPath );
}

std::vector<StrayStaging> sweepOrphans( const std::string &directory, bool remove )
{
  std::error_code ec;
  {
    fs::directory_iterator probe( fs::u8path( directory ), ec );
    if ( ec )
      throw GeoError( ErrorCode::IoError, "sweep: cannot read directory " + directory );
  }

  std::vector<StrayStaging> strays;
  std::set<std::string> reported;

  // Pass 1: staged-shaped files are the anchors of every leftover group.
  std::vector<std::string> stagedMains;
  for ( const fs::directory_entry &entry : fs::directory_iterator( fs::u8path( directory ) ) )
  {
    if ( entry.is_directory() )
      continue;
    const std::string name = [] ( const fs::path &p ) {
      const std::u8string text = p.u8string();
      return std::string( text.begin(), text.end() );
    }( entry.path().filename() );
    if ( !isStagedShapedName( name ) )
      continue;
    const std::string path = directory + "/" + name;
    stagedMains.push_back( path );
    reported.insert( path );
    StrayStaging stray;
    stray.path = path;
    stray.display = ResourceUri::parse( path ).display();
    stray.kind = "staged_file";
    strays.push_back( stray );
  }

  // Pass 2: their sibling sidecars (world files, PAM, shapefile family, our
  // manifest suffix) — minus files that are staged-shaped themselves.
  for ( const std::string &stagedMain : stagedMains )
  {
    for ( const std::string &sidecar : atomic_fs::sidecarsFor( stagedMain ) )
    {
      if ( reported.count( sidecar ) || !atomic_fs::fileExists( sidecar ) )
        continue;
      if ( isStagedShapedName( sidecar.substr( sidecar.rfind( '/' ) + 1 ) ) )
        continue; // covered by pass 1 as its own staged file
      reported.insert( sidecar );
      StrayStaging stray;
      stray.path = sidecar;
      stray.display = ResourceUri::parse( sidecar ).display();
      stray.kind = "staged_sidecar";
      strays.push_back( stray );
    }
  }

  // Pass 3: ledger/manifest sidecars whose dataset main file is gone. A
  // ledger that still describes an OPEN transaction with a LIVING staged
  // file is a LIVE transaction (another process may be mid-run): it is
  // reported as such and never swept.
  std::set<std::string> liveStagedPaths;
  static const char *kOurSuffixes[] = { kStageLedgerSuffix, kFinalizeManifestSuffix };
  for ( const fs::directory_entry &entry : fs::directory_iterator( fs::u8path( directory ) ) )
  {
    if ( entry.is_directory() )
      continue;
    const std::string name = [] ( const fs::path &p ) {
      const std::u8string text = p.u8string();
      return std::string( text.begin(), text.end() );
    }( entry.path().filename() );
    for ( const char *suffix : kOurSuffixes )
    {
      if ( !endsWith( name, suffix ) )
        continue;
      if ( suffix != kStageLedgerSuffix )
      {
        const std::string mainPath = directory + "/" + name.substr( 0, name.size() - std::strlen( suffix ) );
        if ( atomic_fs::fileExists( mainPath ) || reported.count( directory + "/" + name ) )
          break; // healthy sidecar (or already reported as staged sidecar)
        reported.insert( directory + "/" + name );
        StrayStaging stray;
        stray.path = directory + "/" + name;
        stray.display = ResourceUri::parse( stray.path ).display();
        stray.kind = "stale_manifest";
        strays.push_back( stray );
        break;
      }
      // Ledger: staleness requires the staged file to be gone too. A ledger
      // whose staged file still lives is a transaction in flight.
      const std::string mainPath = directory + "/" + name.substr( 0, name.size() - std::strlen( suffix ) );
      if ( atomic_fs::fileExists( mainPath ) || reported.count( directory + "/" + name ) )
        break;
      reported.insert( directory + "/" + name );
      StrayStaging stray;
      stray.path = directory + "/" + name;
      stray.display = ResourceUri::parse( stray.path ).display();
      try
      {
        const StageRecord live = readStageLedger( mainPath );
        if ( live.state == "staged" && atomic_fs::fileExists( live.stagedPath ) )
        {
          liveStagedPaths.insert( live.stagedPath );
          stray.kind = "live_staged";
        }
        else
          stray.kind = "stale_ledger";
      }
      catch ( const GeoError & )
      {
        stray.kind = "stale_ledger"; // unreadable ledger protects nothing
      }
      strays.push_back( stray );
      break;
    }
  }

  if ( remove )
  {
    for ( StrayStaging &stray : strays )
    {
      if ( stray.kind == "live_staged"
           || ( stray.kind == "staged_file" && liveStagedPaths.count( stray.path ) ) )
      {
        stray.removed = false; // a living transaction is never swept
        continue;
      }
      if ( stray.kind == "staged_file" )
        atomic_fs::discardStaged( stray.path ); // removes the sidecar family too
      else if ( stray.kind != "live_staged" )
        atomic_fs::removeFileQuiet( stray.path );
      stray.removed = !atomic_fs::fileExists( stray.path );
    }
  }
  return strays;
}

} // namespace sicnu::geo::io
