// scripts/capability_knowledge_tool.cpp
//
// D8 capability knowledge generator (ADR 0146). One binary, three jobs:
//
//   gen-meta   Derive the v2 capability sidecars for all 111 rs: operators
//              from the LIVE AlgorithmDescriptors and write them under
//              data/processing/algorithm_meta/capability/. Authored keys in
//              existing files (summary / failure_modes / applicability /
//              teaching_use / authored prerequisites & limitations /
//              band_roles / modality / requires_projected) are preserved;
//              everything mechanically derivable is regenerated. Also
//              refreshes the requires_shared_grid list in
//              capability_relations.json from the x-rs-contract facts
//              (gridRelation == "same-grid"), leaving chains/exclusive
//              untouched.
//
//   gen-pages  Render pi/knowledge/capability-*.md from the committed
//              sidecars. --check byte-compares and exits 1 on drift.
//
//   dump       Print one operator's derived v2 block (debugging).
//
// Hand-editing derived fields or the generated pages is a defect (the guard
// test fails the build); authored enrichment goes into the sidecar JSON.

#include <qcoreapplication.h>

#include "agent/harness/capability_catalog.h"
#include "agent/harness/capability_knowledge.h"
#include "agent/harness/capability_pages.h"
#include "agent/harness/capability_relations.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/algorithm_meta_store.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace {

using namespace sicnu::agent::harness;
using sicnu::operators::RSOperatorRegistry;
using sicnu::processing::AlgorithmDescriptor;
using sicnu::processing::AlgorithmMetaStore;
using sicnu::processing::AtomicAlgorithmRegistry;

std::string renderJson( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder[ "indentation" ] = "  ";
  return Json::writeString( builder, value );
}

bool readJsonFile( const QString &path, Json::Value &out )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
    return false;
  const QByteArray raw = file.readAll();
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string errors;
  return reader->parse( raw.constData(), raw.constData() + raw.size(), &out, &errors );
}

bool writeTextFile( const QString &path, const std::string &content )
{
  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  file.write( content.data(), static_cast<qint64>( content.size() ) );
  return true;
}

QString capabilityDir( const QString &root )
{
  return root + "/data/processing/algorithm_meta/capability";
}

/// v1 overlay fields, mirroring AlgorithmMetaStore::entryFromDescriptor
/// semantics (gpu only when declared — #707; accuracy only when reported).
Json::Value v1Overlay( const AlgorithmDescriptor &desc, const Json::Value &existing )
{
  Json::Value doc( Json::objectValue );
  doc[ "id" ] = desc.id;
  doc[ "schema_version" ] = 2;
  doc[ "display_name" ] = desc.displayName;
  doc[ "description" ] = desc.description;
  const auto &meta = desc.agentMetadata;
  if ( !meta.taskFamily.empty() )
    doc[ "task" ] = meta.taskFamily;
  doc[ "input" ] = sicnu::processing::AlgorithmMetaStore::primaryDataKind( desc.inputs );
  doc[ "output" ] = sicnu::processing::AlgorithmMetaStore::primaryDataKind( desc.outputs );
  if ( meta.gpuDeclared )
    doc[ "gpu" ] = meta.gpuAccelerated;
  if ( meta.accuracy >= 0.0 )
    doc[ "accuracy" ] = meta.accuracy;
  if ( !meta.notes.empty() )
    doc[ "notes" ] = meta.notes;
  if ( !meta.tags.empty() )
  {
    Json::Value tags( Json::arrayValue );
    for ( const auto &tag : meta.tags )
      tags.append( tag );
    doc[ "tags" ] = tags;
  }
  // v1 keys the previous sidecar carried but the descriptor no longer does
  // (curated legacy notes) survive so regeneration never silently drops data.
  for ( const std::string &key : existing.getMemberNames() )
  {
    if ( !doc.isMember( key ) && existing[ key ].isString() && !existing[ key ].asString().empty() &&
         ( key == "task" || key == "notes" ) )
      doc[ key ] = existing[ key ];
  }
  return doc;
}

/// Seeds authored modality/band_roles from the preflight knowledge mirror
/// (data/agent/capabilities) when the sidecar does not declare its own: the
/// mirror is the existing authority for those facts; the D8 catalog freezes a
/// copy and the guard test cross-checks both against the operators.
Json::Value seedFromKnowledgeMirror( const Json::Value &authored, const std::string &operatorId,
                                     CapabilityKnowledge &knowledge )
{
  Json::Value seeded = authored;
  const Json::Value mirror = knowledge.entryForOperator( operatorId );
  const bool needsModality = !seeded[ "modality" ].isArray() || seeded[ "modality" ].empty();
  const bool needsRoles = !seeded[ "band_roles" ].isObject() || seeded[ "band_roles" ].empty();
  if ( needsModality && mirror[ "modality" ].isArray() && !mirror[ "modality" ].empty() )
    seeded[ "modality" ] = mirror[ "modality" ];
  if ( needsRoles && mirror[ "band_roles" ].isObject() && !mirror[ "band_roles" ].empty() )
    seeded[ "band_roles" ] = mirror[ "band_roles" ];
  return seeded;
}

int cmdGenMeta( const QString &root )
{
  RSOperatorRegistry::instance();
  sicnu::operators::rs::initBuiltinRsOperators();
  sicnu::operators::rs::installRsOperatorProvider();
  AtomicAlgorithmRegistry::instance().initialize();

  const auto descriptors = AtomicAlgorithmRegistry::instance().listDescriptors();
  std::vector<const AlgorithmDescriptor *> rsDescriptors;
  for ( const auto &desc : descriptors )
  {
    if ( desc.id.rfind( "rs:", 0 ) == 0 )
      rsDescriptors.push_back( &desc );
  }
  if ( rsDescriptors.size() != 111 )
  {
    std::cerr << "gen-meta: expected 111 rs: descriptors, found " << rsDescriptors.size()
              << " — refusing to write a partial catalog\n";
    return 2;
  }

  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  knowledge.setDirectory( root.toStdString() + "/data/agent/capabilities" );
  knowledge.reload();
  if ( !knowledge.loadProblems().empty() )
  {
    for ( const auto &problem : knowledge.loadProblems() )
      std::cerr << "knowledge mirror problem: " << problem << "\n";
    return 2;
  }

  const QString dir = capabilityDir( root );
  QDir().mkpath( dir );

  int written = 0;
  Json::Value sharedGrid( Json::arrayValue );
  for ( const AlgorithmDescriptor *desc : rsDescriptors )
  {
    const std::string fileName = sicnu::processing::AlgorithmMetaStore::idToFileName( desc->id );
    Json::Value existing;
    readJsonFile( dir + QString::fromStdString( "/" + fileName ), existing );

    Json::Value authored = existing[ "capability" ];
    authored = seedFromKnowledgeMirror( authored, desc->id, knowledge );
    Json::Value doc = v1Overlay( *desc, existing );
    doc[ "capability" ] = deriveCapabilityBlock( *desc, authored );

    for ( const Json::Value &row : doc[ "capability" ][ "io" ][ "inputs" ] )
    {
      if ( row.isObject() && row[ "grid_relation" ].isString() &&
           row[ "grid_relation" ].asString() == "same-grid" )
      {
        sharedGrid.append( desc->id );
        break;
      }
    }

    if ( !writeTextFile( dir + QString::fromStdString( "/" + fileName ),
                         renderJson( doc ) + "\n" ) )
    {
      std::cerr << "gen-meta: failed to write " << fileName << "\n";
      return 1;
    }
    ++written;
  }

  // Refresh requires_shared_grid in the relations doc; chains/exclusive stay
  // authored.
  const QString relationsPath = dir + "/capability_relations.json";
  Json::Value relations;
  readJsonFile( relationsPath, relations );
  if ( !relations.isObject() )
    relations = Json::Value( Json::objectValue );
  relations[ "schema_version" ] = 1;
  relations[ "requires_shared_grid" ] = sharedGrid;
  if ( !relations[ "grid_fixer" ].isString() )
    relations[ "grid_fixer" ] = "rs:align";
  if ( !relations[ "chains" ].isArray() )
    relations[ "chains" ] = Json::Value( Json::arrayValue );
  if ( !relations[ "exclusive" ].isArray() )
    relations[ "exclusive" ] = Json::Value( Json::arrayValue );

  std::set<std::string> known;
  for ( const AlgorithmDescriptor *desc : rsDescriptors )
    known.insert( desc->id );
  for ( const std::string &problem : validateRelations( relations, known ) )
  {
    std::cerr << "relations validation problem: " << problem << "\n";
    return 1;
  }
  if ( !writeTextFile( relationsPath, renderJson( relations ) + "\n" ) )
  {
    std::cerr << "gen-meta: failed to write capability_relations.json\n";
    return 1;
  }

  std::cout << "gen-meta: wrote " << written << " capability sidecars, "
            << sharedGrid.size() << " shared-grid operators, relations doc ok\n";
  return 0;
}

int cmdGenPages( const QString &root, bool check )
{
  CapabilityCatalog::instance().setDirectory( capabilityDir( root ).toStdString() );
  CapabilityCatalog::instance().reload();
  for ( const auto &problem : CapabilityCatalog::instance().loadProblems() )
    std::cerr << "catalog problem: " << problem << "\n";
  if ( !CapabilityCatalog::instance().loadProblems().empty() )
    return 2;
  CapabilityRelations::instance().setFilePath(
    ( capabilityDir( root ) + "/capability_relations.json" ).toStdString() );
  CapabilityRelations::instance().reload();
  for ( const auto &problem : CapabilityRelations::instance().loadProblems() )
    std::cerr << "relations problem: " << problem << "\n";
  if ( !CapabilityRelations::instance().loadProblems().empty() )
    return 2;

  int failures = 0;
  for ( const KnowledgePage &page : renderCapabilityKnowledgePages() )
  {
    const QString path = root + "/" + QString::fromStdString( page.relativePath );
    if ( check )
    {
      QFile file( path );
      QString existing;
      if ( file.open( QIODevice::ReadOnly ) )
        existing = QString::fromUtf8( file.readAll() );
      if ( existing.toStdString() != page.content )
      {
        std::cerr << "page drifted: " << page.relativePath
                  << " (regenerate with: capability_knowledge_tool gen-pages)\n";
        ++failures;
      }
    }
    else
    {
      QDir().mkpath( QFileInfo( path ).path() );
      if ( !writeTextFile( path, page.content ) )
      {
        std::cerr << "failed to write " << page.relativePath << "\n";
        ++failures;
      }
    }
  }
  if ( check && failures == 0 )
    std::cout << "gen-pages: zero diff\n";
  else
    std::cout << ( check ? "gen-pages: " : "gen-pages wrote pages, " ) << failures
              << ( check ? " drifted\n" : "\n" );
  return failures == 0 ? 0 : 1;
}

int cmdDump( const QString &root, const std::string &operatorId )
{
  RSOperatorRegistry::instance();
  sicnu::operators::rs::initBuiltinRsOperators();
  sicnu::operators::rs::installRsOperatorProvider();
  AtomicAlgorithmRegistry::instance().initialize();
  for ( const auto &desc : AtomicAlgorithmRegistry::instance().listDescriptors() )
  {
    if ( desc.id != operatorId )
      continue;
    Json::Value existing;
    readJsonFile(
      capabilityDir( root ) + QString::fromStdString(
                                sicnu::processing::AlgorithmMetaStore::idToFileName( desc.id ) ),
      existing );
    Json::Value authored = existing[ "capability" ];
    std::cout << renderJson( deriveCapabilityBlock( desc, authored ) ) << "\n";
    return 0;
  }
  std::cerr << "dump: unknown operator " << operatorId << "\n";
  return 1;
}

void printUsage()
{
  std::cerr << "usage:\n"
               "  capability_knowledge_tool gen-meta <repo-root>\n"
               "  capability_knowledge_tool gen-pages <repo-root> [--check]\n"
               "  capability_knowledge_tool dump <repo-root> <operator-id>\n";
}

} // namespace

int main( int argc, char **argv )
{
  QCoreApplication app( argc, argv );
  if ( argc < 3 )
  {
    printUsage();
    return 64;
  }
  const std::string command = argv[ 1 ];
  const QString root = QString::fromLocal8Bit( argv[ 2 ] );
  if ( command == "gen-meta" )
    return cmdGenMeta( root );
  if ( command == "gen-pages" )
  {
    bool check = false;
    for ( int i = 3; i < argc; ++i )
    {
      if ( std::string( argv[ i ] ) == "--check" )
        check = true;
    }
    return cmdGenPages( root, check );
  }
  if ( command == "dump" && argc >= 4 )
    return cmdDump( root, argv[ 3 ] );
  printUsage();
  return 64;
}
