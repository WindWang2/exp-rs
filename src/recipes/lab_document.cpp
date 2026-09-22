// src/recipes/lab_document.cpp
#include "recipes/lab_document.h"

#include "recipes/lab_source.h" // fnv1a64Hex

#include <json/json.h>

#include <fstream>
#include <memory>
#include <sstream>

namespace sicnu::recipes {

namespace {

bool fail( const std::string &path, const std::string &reason,
           LabDocumentError *error )
{
  if ( error )
    *error = LabDocumentError{ path, reason };
  return false;
}

std::string asString( const Json::Value &v )
{
  return v.isString() ? v.asString() : std::string();
}

/// Tolerant string-list reader: accepts ["a","b"] and [{"name"|"text"|"prompt":…}].
std::vector<std::string> stringList( const Json::Value &v )
{
  std::vector<std::string> out;
  if ( !v.isArray() )
    return out;
  for ( const auto &item : v )
  {
    if ( item.isString() )
      out.push_back( item.asString() );
    else if ( item.isObject() )
      for ( const char *key : { "name", "text", "prompt", "title" } )
        if ( item.isMember( key ) && item[key].isString() )
        {
          out.push_back( item[key].asString() );
          break;
        }
  }
  return out;
}

// ---------------------------------------------------------------------------
// D2: data/labs/<id>.lab.json — spec_version 1|2
// ---------------------------------------------------------------------------
bool parseD2( const Json::Value &root, LabDocument &out, LabDocumentError *error,
              const std::string &path )
{
  out.format = LabFormat::D2;
  out.specVersion = root.get( "spec_version", 0 ).asInt();
  if ( out.specVersion != 1 && out.specVersion != 2 )
    return fail( path, "spec_version must be 1 or 2", error );

  out.id = asString( root["id"] );
  out.title = asString( root["title"] );
  out.titleZh = asString( root["title_zh"] );
  out.objective = asString( root["objective"] );
  out.objectiveZh = asString( root["objective_zh"] );
  if ( out.id.empty() )
    return fail( path, "missing required field: id", error );

  for ( const auto &p : root["prerequisites"].isArray() ? root["prerequisites"]
                                                        : Json::Value( Json::arrayValue ) )
  {
    LabAssetRef ref;
    if ( p.isObject() )
    {
      ref.path = asString( p["path"] );
      ref.note = asString( p["note"] );
    }
    else if ( p.isString() )
      ref.path = p.asString();
    out.prerequisites.push_back( std::move( ref ) );
  }

  out.prerequisiteKnowledge = stringList( root["prerequisite_knowledge"] );

  const Json::Value &steps = root["steps"];
  if ( steps.isArray() )
  {
    int index = 0;
    for ( const auto &s : steps )
    {
      LabStepDoc step;
      step.index = index++;
      if ( !s.isObject() )
        continue;
      step.title = asString( s["title"] );
      step.titleZh = asString( s["title_zh"] );
      step.descriptionZh = asString( s["description_zh"] );
      step.operatorId = asString( s["operator_id"] );
      step.params = s.isMember( "params" ) && s["params"].isObject()
                      ? s["params"] : Json::Value( Json::objectValue );
      step.action = asString( s["action"] );
      step.teachingNote = asString( s["teaching_note"] );
      step.completionHint = asString( s["completion_hint"] );
      out.steps.push_back( std::move( step ) );
    }
  }
  // A missing/empty steps array is NOT a parse error here: lab12–14 ship as
  // step-less v2 wrappers; the source layer resolves them via lab-registry.

  if ( root.isMember( "param_ranges" ) && root["param_ranges"].isObject() )
    out.paramRanges = root["param_ranges"];

  if ( root["expected_artifacts"].isArray() )
    for ( const auto &a : root["expected_artifacts"] )
    {
      LabArtifact artifact;
      if ( a.isObject() )
      {
        artifact.path = asString( a["path"] );
        artifact.kind = asString( a["kind"] );
        artifact.noteZh = asString( a["note_zh"] );
      }
      else if ( a.isString() )
        artifact.path = a.asString();
      if ( !artifact.path.empty() )
        out.expectedArtifacts.push_back( std::move( artifact ) );
    }

  out.gradingRules = asString( root["grading_rules"] );
  if ( root["grading_ref"].isObject() )
    out.gradingPipeline = asString( root["grading_ref"]["pipeline"] );

  for ( const auto &q : stringList( root["thinking_questions"] ) )
    out.questions.push_back( LabQuestion{ q, {} } );

  if ( root["glossary"].isArray() )
    for ( const auto &g : root["glossary"] )
      if ( g.isObject() && g["term"].isString() )
        out.glossaryTerms.push_back( g["term"].asString() );

  if ( root["principles"].isArray() )
    for ( const auto &p : root["principles"] )
      if ( p.isObject() && p["heading"].isString() )
        out.principleHeadings.push_back( p["heading"].asString() );

  return true;
}

// ---------------------------------------------------------------------------
// D3: data/labs/<id>.labspec.json — schema "sicnu.labspec.v1"
// ---------------------------------------------------------------------------
bool parseD3( const Json::Value &root, LabDocument &out, LabDocumentError *error,
              const std::string &path )
{
  out.format = LabFormat::D3;
  out.schemaTag = asString( root["schema"] );

  out.id = asString( root["id"] );
  out.title = asString( root["title"] );
  out.titleZh = asString( root["title_zh"] );
  if ( out.titleZh.empty() )
    out.titleZh = out.title; // D3 titles are authored in zh already
  out.theme = asString( root["theme"] );
  out.audience = asString( root["audience"] );
  out.durationMinutes = root.get( "duration_minutes", 0 ).asInt();
  if ( out.id.empty() )
    return fail( path, "missing required field: id", error );

  const auto objectives = stringList( root["objectives"] );
  for ( const auto &o : objectives )
  {
    if ( out.objective.empty() )
      out.objective = o;
    else
      out.objective += "\n" + o;
  }
  out.objectiveZh = out.objective; // D3 objectives are authored in zh

  // D3 prerequisites: knowledge strings (data lives under `data.spec_ref`).
  out.prerequisiteKnowledge = stringList( root["prerequisites"] );
  for ( const auto &dep : stringList( root["dependencies"] ) )
    out.prerequisiteKnowledge.push_back( dep );

  if ( root["data"].isObject() )
  {
    out.dataSpecRef = asString( root["data"]["spec_ref"] );
    LabAssetRef ref;
    ref.path = out.dataSpecRef;
    ref.note = asString( root["data"]["description"] );
    if ( !ref.path.empty() )
      out.prerequisites.push_back( std::move( ref ) );
  }

  if ( root["pipeline"].isObject() )
  {
    out.gradingPipeline = asString( root["pipeline"]["ref"] );
    out.pipelineRunner = asString( root["pipeline"]["runner"] );
  }
  if ( root["grading_ref"].isObject() )
    out.gradingIntentRef = asString( root["grading_ref"]["intent_ref"] );

  if ( root["operators"].isArray() )
    for ( const auto &o : root["operators"] )
      if ( o.isObject() )
      {
        const std::string id = asString( o["operator_id"] );
        const std::string role = asString( o["role"] );
        if ( !id.empty() )
          out.operatorRoles.push_back( role.empty() ? id : id + " — " + role );
      }

  const Json::Value &steps = root["steps"];
  if ( steps.isArray() )
  {
    int index = 0;
    for ( const auto &s : steps )
    {
      LabStepDoc step;
      step.index = index++;
      if ( !s.isObject() )
        continue;
      step.id = asString( s["id"] );
      step.title = asString( s["title"] );
      step.titleZh = step.title;
      step.descriptionZh = asString( s["detail"] );
      step.operatorId = asString( s["operator_id"] );
      step.params = s.isMember( "params" ) && s["params"].isObject()
                      ? s["params"] : Json::Value( Json::objectValue );
      step.headlessNote = asString( s["headless_note"] );
      step.teachingNote = asString( s["teaching_note"] );
      step.completionHint = asString( s["completion_hint"] );
      out.steps.push_back( std::move( step ) );
    }
  }

  if ( root["expected_results"].isArray() )
    for ( const auto &e : root["expected_results"] )
    {
      LabExpectedResult expected;
      if ( e.isObject() )
      {
        expected.claim = asString( e["claim"] );
        expected.artifact = asString( e["artifact"] );
        expected.toleranceNote = asString( e["tolerance_note"] );
      }
      if ( !expected.claim.empty() || !expected.artifact.empty() )
        out.expectedResults.push_back( std::move( expected ) );
    }

  if ( root["questions"].isArray() )
    for ( const auto &q : root["questions"] )
      if ( q.isObject() )
        out.questions.push_back( LabQuestion{ asString( q["prompt"] ), asString( q["hint"] ) } );
      else if ( q.isString() )
        out.questions.push_back( LabQuestion{ q.asString(), {} } );

  if ( root["glossary"].isArray() )
    for ( const auto &g : root["glossary"] )
      if ( g.isObject() && g["term"].isString() )
        out.glossaryTerms.push_back( g["term"].asString() );

  if ( root["principles"].isArray() )
    for ( const auto &p : root["principles"] )
      if ( p.isObject() && p["heading"].isString() )
        out.principleHeadings.push_back( p["heading"].asString() );

  return true;
}

} // namespace

std::string LabDocumentError::toString() const
{
  return path + ": " + reason;
}

bool parseLabDocument( const std::string &jsonText, const std::string &path,
                       LabDocument &out, LabDocumentError *error )
{
  Json::Value root;
  Json::CharReaderBuilder builder;
  builder["collectComments"] = false;
  builder["stackLimit"] = 256;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string errors;
  if ( !reader->parse( jsonText.data(), jsonText.data() + jsonText.size(), &root, &errors ) )
    return fail( path, "invalid JSON: " + errors, error );
  if ( !root.isObject() )
    return fail( path, "root must be an object", error );

  out = LabDocument{};
  out.sourcePath = path;
  out.sourceFingerprint = fnv1a64Hex( jsonText );

  if ( root["spec_version"].isInt() || root["spec_version"].isUInt() )
    return parseD2( root, out, error, path );
  if ( root["schema"].isString() )
  {
    if ( root["schema"].asString() == "sicnu.labspec.v1" )
      return parseD3( root, out, error, path );
    return fail( path, "unrecognized schema: " + root["schema"].asString(), error );
  }
  return fail( path, "unrecognized lab format: need spec_version (D2) or schema (D3)", error );
}

bool loadLabDocumentFile( const std::string &path, LabDocument &out,
                          LabDocumentError *error )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in )
    return fail( path, "cannot open file", error );
  std::stringstream buffer;
  buffer << in.rdbuf();
  return parseLabDocument( buffer.str(), path, out, error );
}

} // namespace sicnu::recipes
