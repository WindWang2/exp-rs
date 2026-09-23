// src/recipes/lab_source.cpp
#include "recipes/lab_source.h"

#include "recipes/recipe_diagnostics.h"

#include <json/json.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace sicnu::recipes {

namespace fs = std::filesystem;

std::string fnv1a64Hex( const std::string &text )
{
  // FNV-1a 64: deterministic non-cryptographic fingerprint for drift checks.
  unsigned long long hash = 14695981039346656037ULL;
  for ( const unsigned char c : text )
  {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << hash;
  std::string hex = out.str();
  return std::string( 16 - std::min<std::size_t>( 16, hex.size() ), '0' ) + hex;
}

namespace {

std::string readFile( const fs::path &path )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in )
    return {};
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

/// Sorted list of files under `dir` matching `suffix`.
std::vector<fs::path> filesWithSuffix( const fs::path &dir, const std::string &suffix )
{
  std::vector<fs::path> out;
  std::error_code ec;
  if ( !fs::is_directory( dir, ec ) )
    return out;
  for ( const auto &entry : fs::directory_iterator( dir, ec ) )
  {
    if ( ec )
      break;
    if ( entry.is_regular_file() && entry.path().filename().string().size() >= suffix.size() &&
         entry.path().filename().string().compare(
           entry.path().filename().string().size() - suffix.size(),
           suffix.size(), suffix ) == 0 )
      out.push_back( entry.path() );
  }
  std::sort( out.begin(), out.end() );
  return out;
}

/// Provenance paths must be machine-independent (committed artifacts are
/// byte-compared). When `path` lives under the repo root — conventionally
/// `labsDir` == <root>/data/labs — return it repo-relative; otherwise just
/// the filename (enough provenance for out-of-tree scans).
std::string repoRelativePath( const fs::path &path, const fs::path &labsDir )
{
  std::error_code ec;
  const fs::path abs = fs::weakly_canonical( path, ec );
  const fs::path root = fs::weakly_canonical( labsDir.parent_path().parent_path(), ec );
  if ( ec || abs.empty() || root.empty() )
    return path.filename().string();
  const fs::path rel = fs::relative( abs, root, ec );
  if ( ec || rel.empty() || rel.generic_string().rfind( "..", 0 ) == 0 )
    return path.filename().string();
  return rel.generic_string();
}

/// lab-registry.json view: canonicalId → source path (D3 .labspec.json), and
/// alias → canonicalId so D3 file stems map back to their canonical lab.
struct RegistryView
{
  std::map<std::string, std::string> canonicalToSource;
  std::map<std::string, std::string> aliasToCanonical;
  bool present = false;
};

RegistryView loadRegistry( const fs::path &registryPath )
{
  RegistryView view;
  const std::string text = readFile( registryPath );
  if ( text.empty() )
    return view;
  Json::Value root;
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string errors;
  if ( !reader->parse( text.data(), text.data() + text.size(), &root, &errors ) ||
       !root.isObject() || !root["canonical"].isObject() )
    return view;

  for ( const std::string &canonicalId : root["canonical"].getMemberNames() )
  {
    const Json::Value &entry = root["canonical"][canonicalId];
    if ( !entry.isObject() )
      continue;
    const Json::Value &source = entry[ "source" ];
    // isString guard: asString() on a hostile non-string raises LogicError.
    if ( source.isString() && !source.asString().empty() )
      view.canonicalToSource[canonicalId] = source.asString();
    if ( entry["aliases"].isArray() )
      for ( const auto &alias : entry["aliases"] )
        if ( alias.isString() )
          view.aliasToCanonical[alias.asString()] = canonicalId;
  }
  view.present = true;
  return view;
}

/// Merge v2 teaching metadata from a step-less wrapper into the resolved D3
/// document. Canonical id, v2-only fields and grading pointers move over; the
/// D3 executable content (steps/expected_results/questions) is authoritative.
LabDocument mergeWrapperOverD3( const LabDocument &wrapper, LabDocument d3 )
{
  LabDocument merged = std::move( d3 );
  merged.id = wrapper.id;                 // canonical id wins
  merged.titleZh = wrapper.titleZh.empty() ? merged.titleZh : wrapper.titleZh;
  merged.objective = wrapper.objective.empty() ? merged.objective : wrapper.objective;
  merged.objectiveZh = wrapper.objectiveZh.empty() ? merged.objectiveZh : wrapper.objectiveZh;
  merged.specVersion = wrapper.specVersion;
  if ( !wrapper.gradingRules.empty() )
    merged.gradingRules = wrapper.gradingRules;
  if ( !wrapper.gradingPipeline.empty() )
    merged.gradingPipeline = wrapper.gradingPipeline;
  for ( const auto &k : wrapper.prerequisiteKnowledge )
    if ( std::find( merged.prerequisiteKnowledge.begin(), merged.prerequisiteKnowledge.end(), k ) ==
         merged.prerequisiteKnowledge.end() )
      merged.prerequisiteKnowledge.push_back( k );
  // Wrapper v2 artifacts are the authored contract for graded outputs.
  if ( !wrapper.expectedArtifacts.empty() )
    merged.expectedArtifacts = wrapper.expectedArtifacts;
  // Keep BOTH provenances: merged.sourcePath already points at the D3 file
  // that supplied executable content (set by parseLabDocument); wrapperPath
  // records the canonical v2 wrapper that contributed teaching metadata.
  merged.wrapperPath = wrapper.sourcePath;
  return merged;
}

} // namespace

std::vector<LabSourceEntry> loadLabDirectory( const std::string &labsDir,
                                              const std::string &registryPath,
                                              std::vector<LabDocumentError> &errors )
{
  const fs::path root( labsDir );
  const RegistryView registry = registryPath.empty()
                                  ? RegistryView{}
                                  : loadRegistry( registryPath );

  std::vector<LabSourceEntry> entries;
  std::set<std::string> consumedD3; // stems merged into a canonical wrapper

  // Pass 1: canonical .lab.json files.
  for ( const auto &path : filesWithSuffix( root, ".lab.json" ) )
  {
    LabDocument doc;
    LabDocumentError error;
    if ( !loadLabDocumentFile( path.string(), doc, &error ) )
    {
      errors.push_back( error );
      continue;
    }
    LabSourceEntry entry;
    entry.canonicalId = doc.id;
    entry.sourcePath = path.string();
    doc.sourcePath = repoRelativePath( path, root );
    entry.document = std::move( doc );

    // Step-less wrapper → resolve the D3 source via the registry.
    if ( entry.document.steps.empty() )
    {
      const auto it = registry.canonicalToSource.find( entry.canonicalId );
      if ( it != registry.canonicalToSource.end() )
      {
        // Registry `source` is repo-root relative; labsDir is typically
        // <root>/data/labs, so repo root = labsDir/../..
        const fs::path d3Local = fs::path( it->second ).is_absolute()
                                   ? fs::path( it->second )
                                   : ( fs::path( labsDir ).parent_path().parent_path() / it->second );
        LabDocument d3;
        LabDocumentError d3Error;
        if ( loadLabDocumentFile( d3Local.string(), d3, &d3Error ) )
        {
          d3.sourcePath = repoRelativePath( d3Local, root );
          entry.document = mergeWrapperOverD3( entry.document, std::move( d3 ) );
          entry.sourcePath = d3Local.string();
          entry.resolvedViaRegistry = true;
          consumedD3.insert( d3Local.filename().string() );
        }
        else
        {
          // Typed: the registry promised a source we could not load.
          errors.push_back( LabDocumentError{
            d3Local.string(),
            std::string( diag_codes::kUnresolvedRegistrySource ) + ": " +
              d3Error.reason } );
        }
      }
      // No registry source → entry stays step-less; compiler emits no_steps.
    }
    entries.push_back( std::move( entry ) );
  }

  // Pass 2: standalone D3 .labspec.json files not consumed by a wrapper.
  for ( const auto &path : filesWithSuffix( root, ".labspec.json" ) )
  {
    const std::string filename = path.filename().string();
    if ( consumedD3.count( filename ) )
      continue;
    LabDocument doc;
    LabDocumentError error;
    if ( !loadLabDocumentFile( path.string(), doc, &error ) )
    {
      errors.push_back( error );
      continue;
    }
    LabSourceEntry entry;
    // Prefer the canonical id when this D3 file is a registered alias.
    const std::string stem = filename.substr( 0, filename.size() - std::string( ".labspec.json" ).size() );
    const auto alias = registry.aliasToCanonical.find( doc.id );
    const auto stemAlias = registry.aliasToCanonical.find( stem );
    entry.canonicalId = alias != registry.aliasToCanonical.end() ? alias->second
                        : stemAlias != registry.aliasToCanonical.end() ? stemAlias->second
                        : doc.id;
    entry.sourcePath = path.string();
    doc.sourcePath = repoRelativePath( path, root );
    entry.document = std::move( doc );
    entries.push_back( std::move( entry ) );
  }

  std::sort( entries.begin(), entries.end(),
             []( const LabSourceEntry &a, const LabSourceEntry &b )
             { return a.canonicalId < b.canonicalId; } );
  return entries;
}

std::string defaultLabDirectory()
{
  if ( const char *env = std::getenv( "SICNU_LAB_DIR" ) )
    if ( *env && fs::is_directory( env ) )
      return env;

  const fs::path cwd = fs::current_path() / "data" / "labs";
  if ( fs::is_directory( cwd ) )
    return cwd.string();

#ifdef SICNU_SOURCE_DIR
  const fs::path source = fs::path( SICNU_SOURCE_DIR ) / "data" / "labs";
  if ( fs::is_directory( source ) )
    return source.string();
#endif
  return {};
}

} // namespace sicnu::recipes
