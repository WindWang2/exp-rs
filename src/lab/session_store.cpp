/***************************************************************************
  lab/session_store.cpp — atomic persistence for LabSessions. See the header.
***************************************************************************/

#include "session_store.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#else
#include <process.h>
#include <windows.h>
#endif

#include <filesystem>
#include <sys/types.h>

namespace sicnu::lab
{
namespace
{

bool isSafeSessionName( const std::string &s )
{
  if ( s.empty() || s.size() > 64 || s == "." || s == ".." )
    return false;
  for ( const char ch : s )
  {
    const bool ok = ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' ) ||
                    ( ch >= '0' && ch <= '9' ) || ch == '_' || ch == '-' || ch == '.';
    if ( !ok )
      return false;
  }
  return true;
}

/// "<labId>/<studentId>/<seq>" → "<labId>/<studentId>-<seq>.session.json".
/// Components are re-validated here: a sessionId coming off disk is untrusted
/// input, and it must never shape a filesystem path outside the store root.
bool sessionPathFromId( const std::string &rootDir, const std::string &sessionId,
                        std::string &outPath )
{
  std::istringstream stream( sessionId );
  std::string labId, studentId, seq;
  std::getline( stream, labId, '/' );
  std::getline( stream, studentId, '/' );
  std::getline( stream, seq, '/' );
  if ( !stream.eof() && !stream.fail() )
  {
    std::string extra;
    std::getline( stream, extra, '/' );
    if ( !extra.empty() )
      return false;
  }
  if ( !isSafeSessionName( labId ) || !isSafeSessionName( studentId ) )
    return false;
  if ( seq.empty() || seq.size() > 18 )
    return false;
  for ( const char ch : seq )
  {
    if ( ch < '0' || ch > '9' )
      return false;
  }
  outPath = rootDir + "/" + labId + "/" + studentId + "-" + seq + ".session.json";
  return true;
}

bool writeFileSync( const std::string &path, const std::string &bytes, std::string &error )
{
#if !defined(_WIN32)
  const int fd = ::open( path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644 );
  if ( fd < 0 )
  {
    error = std::string( "cannot open for write: " ) + std::strerror( errno );
    return false;
  }
  std::size_t written = 0;
  while ( written < bytes.size() )
  {
    const ssize_t n = ::write( fd, bytes.data() + written, bytes.size() - written );
    if ( n < 0 )
    {
      if ( errno == EINTR )
        continue;
      error = std::string( "write failed: " ) + std::strerror( errno );
      ::close( fd );
      return false;
    }
    written += static_cast<std::size_t>( n );
  }
  if ( ::fsync( fd ) != 0 )
  {
    error = std::string( "fsync failed: " ) + std::strerror( errno );
    ::close( fd );
    return false;
  }
  ::close( fd );
  return true;
#else
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  if ( !out.good() )
  {
    error = "cannot open for write";
    return false;
  }
  out.write( bytes.data(), static_cast<std::streamsize>( bytes.size() ) );
  out.flush();
  if ( !out.good() )
  {
    error = "short write";
    return false;
  }
  return true;
#endif
}

std::unique_ptr<Json::CharReader> makeStrictReader()
{
  // Same hardening discipline as the plugin manifests: a depth bomb is a
  // typed rejection, not a crash (unlike the legacy unbounded Json::Reader).
  Json::CharReaderBuilder builder;
  builder[ "stackLimit" ] = 64;
  builder[ "collectComments" ] = false;
  return std::unique_ptr<Json::CharReader>( builder.newCharReader() );
}

bool readJsonFile( const std::string &path, Json::Value &out, std::string &error )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in.good() )
  {
    error = "cannot read file";
    return false;
  }
  std::string bytes( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
  Json::Value doc;
  std::string parseErrors;
  if ( !makeStrictReader()->parse( bytes.data(), bytes.data() + bytes.size(), &doc, &parseErrors ) )
  {
    error = "invalid JSON: " + parseErrors;
    return false;
  }
  out = std::move( doc );
  return true;
}

LabResult<LabSession> loadFromPath( const std::string &path )
{
  Json::Value doc;
  std::string error;
  if ( !readJsonFile( path, doc, error ) )
    return LabResult<LabSession>::failure(
      { LabDiag{ "lab.session.corrupt", path + ": " + error } } );
  return sessionFromJson( doc );
}

std::atomic<uint64_t> s_tmpCounter{ 0 };

} // namespace

LabSessionStore::LabSessionStore( std::string rootDir )
  : m_rootDir( std::move( rootDir ) )
{
}

std::string LabSessionStore::defaultRoot()
{
#if !defined(_WIN32)
  if ( const char *env = ::getenv( "SICNU_LAB_SESSION_DIR" ) )
    return env;
  char buffer[ 4096 ];
  if ( ::getcwd( buffer, sizeof( buffer ) ) )
    return std::string( buffer ) + "/.sicnu/lab/sessions";
  return std::string( ".sicnu/lab/sessions" );
#else
  if ( const char *env = std::getenv( "SICNU_LAB_SESSION_DIR" ) )
    return env;
  char buffer[ MAX_PATH ];
  if ( GetCurrentDirectoryA( MAX_PATH, buffer ) )
    return std::string( buffer ) + "\\.sicnu\\lab\\sessions";
  return std::string( ".sicnu\\lab\\sessions" );
#endif
}

std::string LabSessionStore::sessionPath( const std::string &sessionId,
                                          std::vector<LabDiag> &diags ) const
{
  std::string path;
  if ( !sessionPathFromId( m_rootDir, sessionId, path ) )
    diags.push_back( LabDiag{ "lab.session.field",
                              "sessionId '" + sessionId + "' is not a safe store key" } );
  return path;
}

LabResult<LabSession> LabSessionStore::create( const LabRuntimePlan &plan, const SessionMeta &meta,
                                               const std::string &specFingerprint )
{
  std::vector<LabDiag> diags;
  if ( !isSafeSessionName( meta.labId ) || !isSafeSessionName( meta.studentId ) )
  {
    diags.push_back( LabDiag{ "lab.session.field",
                              "labId/studentId must be safe identity components" } );
    return LabResult<LabSession>::failure( std::move( diags ) );
  }

  long long next = 1;
  const std::string dir = m_rootDir + "/" + meta.labId;
  std::error_code ec;
  if ( std::filesystem::exists( dir, ec ) )
  {
    for ( const std::filesystem::directory_entry &entry :
          std::filesystem::directory_iterator( dir, ec ) )
    {
      if ( ec )
        break;
      const std::string name = entry.path().filename().string();
      const std::string prefix = meta.studentId + "-";
      const std::string suffix = ".session.json";
      if ( name.size() <= prefix.size() + suffix.size() ||
           name.compare( 0, prefix.size(), prefix ) != 0 ||
           name.compare( name.size() - suffix.size(), suffix.size(), suffix ) != 0 )
        continue;
      const std::string digits = name.substr( prefix.size(), name.size() - prefix.size() - suffix.size() );
      if ( digits.empty() ||
           digits.find_first_not_of( "0123456789" ) != std::string::npos )
        continue;
      const long long value = std::strtoll( digits.c_str(), nullptr, 10 );
      if ( value > 0 && value >= next )
        next = value + 1;
    }
  }

  auto started = startSession( plan, meta, next );
  if ( !started.ok )
    return started;
  started.value.labSpecFingerprint = specFingerprint;
  return LabResult<LabSession>::success( std::move( started.value ) );
}

LabResult<> LabSessionStore::save( const LabSession &session )
{
  std::vector<LabDiag> diags;
  const std::string finalPath = sessionPath( session.sessionId, diags );
  if ( !diags.empty() )
    return LabResult<>::failure( std::move( diags ) );

  std::error_code ec;
  std::filesystem::create_directories(
    std::filesystem::path( finalPath ).parent_path(), ec );
  if ( ec )
    return LabResult<>::failure( { LabDiag{ "lab.session.io",
                                            "cannot create store directory: " + ec.message() } } );

  const std::string bytes = sessionToCanonicalBytes( session );
  // Unique per-save tmp name: concurrent saves of the same session can never
  // interleave on a shared tmp file (WorkflowCheckpointManager discipline).
#ifdef _WIN32
  const int pid = ::_getpid();
#else
  const int pid = ::getpid();
#endif
  const std::string tmpPath = finalPath + ".tmp." + std::to_string( pid ) + "." +
                              std::to_string( s_tmpCounter.fetch_add( 1 ) );
  std::string error;
  if ( !writeFileSync( tmpPath, bytes, error ) )
  {
    std::filesystem::remove( tmpPath, ec );
    return LabResult<>::failure( { LabDiag{ "lab.session.io", error } } );
  }
  // rename(2) / MoveFileEx(REPLACE_EXISTING): atomic replace, old-or-new.
  std::filesystem::rename( std::filesystem::path( tmpPath ), std::filesystem::path( finalPath ), ec );
  if ( ec )
  {
    std::filesystem::remove( tmpPath, ec );
    return LabResult<>::failure( { LabDiag{ "lab.session.io",
                                            "rename failed: " + ec.message() } } );
  }
  return LabResult<>::success();
}

LabResult<LabSession> LabSessionStore::load( const std::string &sessionId,
                                             const std::string &currentSpecFingerprint ) const
{
  auto loaded = loadAny( sessionId );
  if ( !loaded.ok )
    return loaded;
  if ( !currentSpecFingerprint.empty() &&
       loaded.value.labSpecFingerprint != currentSpecFingerprint )
  {
    return LabResult<LabSession>::failure( { LabDiag{
      "lab.session.spec_drift",
      "session '" + sessionId + "' was started against spec fingerprint '" +
        loaded.value.labSpecFingerprint + "', current spec is '" + currentSpecFingerprint +
        "' — refusing silent mismatch" } } );
  }
  return loaded;
}

LabResult<LabSession> LabSessionStore::loadAny( const std::string &sessionId ) const
{
  std::vector<LabDiag> diags;
  const std::string path = sessionPath( sessionId, diags );
  if ( !diags.empty() )
    return LabResult<LabSession>::failure( std::move( diags ) );

  std::error_code ec;
  if ( !std::filesystem::exists( path, ec ) )
    return LabResult<LabSession>::failure(
      { LabDiag{ "lab.session.not_found", "no session file at '" + path + "'" } } );
  return loadFromPath( path );
}

std::vector<std::string> LabSessionStore::listSessionIds() const
{
  std::vector<std::string> ids;
  const Listing listing = listSessions();
  ids.reserve( listing.summaries.size() );
  for ( const Summary &summary : listing.summaries )
    ids.push_back( summary.sessionId );
  return ids;
}

LabSessionStore::Listing LabSessionStore::listSessions() const
{
  Listing listing;
  std::error_code ec;
  if ( !std::filesystem::exists( m_rootDir, ec ) )
    return listing;
  std::vector<std::filesystem::path> files;
  for ( const std::filesystem::directory_entry &labDir :
        std::filesystem::directory_iterator( m_rootDir, ec ) )
  {
    if ( ec )
      break;
    if ( !labDir.is_directory() )
      continue;
    for ( const std::filesystem::directory_entry &entry :
          std::filesystem::directory_iterator( labDir.path(), ec ) )
    {
      if ( ec )
        break;
      if ( entry.is_regular_file() && entry.path().extension() == ".json" &&
           entry.path().filename().string().ends_with( ".session.json" ) )
        files.push_back( entry.path() );
    }
  }

  for ( const std::filesystem::path &file : files )
  {
    auto loaded = loadFromPath( file.string() );
    if ( !loaded.ok )
    {
      for ( const LabDiag &diag : loaded.diagnostics )
        listing.warnings.push_back( diag );
      continue;
    }
    Summary summary;
    summary.sessionId = loaded.value.sessionId;
    summary.labId = loaded.value.labId;
    summary.studentId = loaded.value.studentId;
    summary.state = loaded.value.state == SessionState::Active     ? "active"
                    : loaded.value.state == SessionState::Completed ? "completed"
                                                                    : "abandoned";
    summary.planSource = loaded.value.planSource;
    summary.lastSeq = loaded.value.lastSeq;
    listing.summaries.push_back( std::move( summary ) );
  }

  std::sort( listing.summaries.begin(), listing.summaries.end(),
             []( const Summary &a, const Summary &b ) { return a.sessionId < b.sessionId; } );
  return listing;
}

} // namespace sicnu::lab
