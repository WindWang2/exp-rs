#include "explain/adapters/provenance_file_evidence.h"

#include "explain/explain_provenance.h"
#include "workflow/workflow_provenance.h"

#include <QJsonDocument>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace sicnu::explain::adapters
{
namespace
{

std::string toStd( const QString &text )
{
  return text.toStdString();
}

// The path never crosses into QString: std::ifstream consumes the
// std::filesystem::path natively, which keeps non-UTF-8 narrow encodings
// (Windows ACP) intact — the guidance store loader uses the same discipline.
bool readFileCapped( const std::filesystem::path &path, QByteArray &out, std::string &error )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in )
  {
    error = "cannot open file";
    return false;
  }
  in.seekg( 0, std::ios::end );
  const std::streamoff size = in.tellg();
  if ( size < 0 || static_cast<size_t>( size ) > ProvenanceFileEvidence::kMaxFileBytes )
  {
    error = "file exceeds the provenance size bound";
    return false;
  }
  in.seekg( 0, std::ios::beg );
  std::string bytes( static_cast<size_t>( size ), '\0' );
  in.read( bytes.data(), size );
  if ( !in || bytes.empty() )
  {
    error = "file is empty";
    return false;
  }
  out = QByteArray::fromStdString( bytes );
  return true;
}

} // namespace

std::unique_ptr<ProvenanceFileEvidence> ProvenanceFileEvidence::loadFromDirectory(
  const std::string &directory, std::vector<EvidenceLoadProblem> &problems )
{
  auto adapter = std::unique_ptr<ProvenanceFileEvidence>( new ProvenanceFileEvidence() );

  std::error_code ec;
  if ( !std::filesystem::exists( directory, ec ) || !std::filesystem::is_directory( directory, ec ) )
  {
    problems.push_back( { directory, "directory_missing",
                          "provenance directory does not exist" } );
    adapter->problems_ = problems;
    return adapter;
  }

  std::vector<std::filesystem::path> files;
  for ( const std::filesystem::directory_entry &entry :
        std::filesystem::directory_iterator( directory, ec ) )
  {
    if ( ec )
      break;
    const std::string name = entry.path().filename().string();
    if ( entry.is_regular_file()
         && name.rfind( "provenance_", 0 ) == 0
         && name.size() > 5
         && name.compare( name.size() - 5, 5, ".json" ) == 0 )
      files.push_back( entry.path() );
  }
  std::sort( files.begin(), files.end() );

  for ( const std::filesystem::path &file : files )
  {
    const std::string fileName = file.filename().string();
    const std::string runId = fileName.substr( 11, fileName.size() - 11 - 5 ); // strip prefix/suffix

    // The evidence-link grammar (provenance:<runId>#node:<nodeId>) admits no
    // whitespace and exactly one '#': a record whose run id cannot produce a
    // valid link is refused here instead of failing closed much later.
    if ( runId.empty() || runId.find( '#' ) != std::string::npos
         || containsWhitespace( runId ) )
    {
      adapter->problems_.push_back( { fileName, "malformed_name",
                                      "file name does not carry a usable run id" } );
      continue;
    }

    if ( adapter->runs_.size() >= kMaxRuns )
    {
      adapter->problems_.push_back( { fileName, "run_limit_exceeded",
                                      "run record bound reached; remaining files skipped" } );
      break;
    }

    QByteArray bytes;
    std::string error;
    if ( !readFileCapped( file, bytes, error ) )
    {
      adapter->problems_.push_back(
        { fileName, error.find( "exceeds" ) != std::string::npos ? "file_too_large"
                                                                 : "file_unreadable",
          error } );
      continue;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson( bytes, &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() )
    {
      adapter->problems_.push_back(
        { fileName, "parse_failed", parseError.errorString().toStdString() } );
      continue;
    }

    // ProvenanceGraph::fromJson is the strict reader: a refused envelope
    // carries its typed reason, which is recorded instead of a bare refusal.
    const sicnu::workflow::Result<sicnu::workflow::ProvenanceGraph> parsed =
      sicnu::workflow::ProvenanceGraph::fromJson( document.object() );
    if ( !parsed.isSuccess() )
    {
      adapter->problems_.push_back(
        { fileName, "envelope_refused", parsed.error().toStdString() } );
      continue;
    }
    auto graph = std::make_shared<const sicnu::workflow::ProvenanceGraph>( parsed.value() );

    if ( std::any_of( adapter->runs_.begin(), adapter->runs_.end(),
                      [ &runId ]( const RunRecord &r ) { return r.runId == runId; } ) )
    {
      adapter->problems_.push_back(
        { fileName, "duplicate_run", "run '" + runId + "' already loaded; first wins" } );
      continue;
    }

    adapter->runs_.push_back( RunRecord{ runId, graph } );
  }

  problems = adapter->problems_;
  return adapter;
}

std::optional<StepEvidence> ProvenanceFileEvidence::evidenceFor( const std::string &runId,
                                                                 const std::string &stepId ) const
{
  if ( runId.empty() || stepId.empty() )
    return std::nullopt;

  const auto runIt = std::find_if( runs_.begin(), runs_.end(),
                                   [ &runId ]( const RunRecord &r ) { return r.runId == runId; } );
  if ( runIt == runs_.end() )
    return std::nullopt;

  const sicnu::workflow::ProvenanceGraph &graph = *runIt->graph;
  const QString nodeExecId = QStringLiteral( "node:%1" ).arg( QString::fromStdString( stepId ) );

  const sicnu::workflow::ProvenanceNode *exec = nullptr;
  for ( const sicnu::workflow::ProvenanceNode &node : graph.nodes() )
  {
    if ( node.id == nodeExecId && node.kind == QStringLiteral( "nodeExec" ) )
    {
      exec = &node;
      break;
    }
  }
  if ( exec == nullptr )
    return std::nullopt;

  StepEvidence evidence;
  const QJsonObject &attributes = exec->attributes;
  evidence.status = toStd( attributes[QStringLiteral( "state" )].toString() );
  if ( attributes[QStringLiteral( "elapsedMs" )].isDouble() )
    evidence.elapsedMs = static_cast<long long>( attributes[QStringLiteral( "elapsedMs" )].toDouble() );
  if ( attributes[QStringLiteral( "isCacheHit" )].isBool() )
    evidence.cacheHit = attributes[QStringLiteral( "isCacheHit" )].toBool();
  evidence.errorMessage = toStd( attributes[QStringLiteral( "errorMessage" )].toString() );

  // The record carries no wall-clock stamps; both stay empty (unknown).
  const QString execId = exec->id;
  QStringList artifactIds = graph.producedBy( execId );
  if ( artifactIds.isEmpty() )
    artifactIds = graph.reusedBy( execId );
  if ( !artifactIds.isEmpty() )
  {
    for ( const sicnu::workflow::ProvenanceNode &node : graph.nodes() )
    {
      if ( node.id != artifactIds.front() || node.kind != QStringLiteral( "artifact" ) )
        continue;
      evidence.artifactPath = toStd( node.attributes[QStringLiteral( "path" )].toString() );
      evidence.artifactDigest = toStd( node.attributes[QStringLiteral( "fingerprint" )].toString() );
      break;
    }
  }

  EvidenceLink link;
  link.kind = EvidenceProvenance;
  link.target = "provenance:" + runId + "#node:" + stepId;
  evidence.links.push_back( link );

  return evidence;
}

} // namespace sicnu::explain::adapters
