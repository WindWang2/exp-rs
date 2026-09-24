#include "lab_validate_service.h"

#include "agent/output_verifier.h"

#include <filesystem>
#include <QString>
#include <QStringList>

namespace sicnu::app::teaching {
namespace {

Json::Value verifierLensJson( const sicnu::agent::OutputVerification &v )
{
  Json::Value doc( Json::objectValue );
  doc["status"] = v.ok ? "pass" : "fail";
  QStringList problems = v.issues;
  for ( const QString &w : v.warnings ) problems << QStringLiteral( "警告: " ) + w;
  doc["reason_zh"] = problems.join( QStringLiteral( "; " ) ).toStdString();
  doc["summary"] = v.summary;
  return doc;
}

} // namespace

std::string resolveLabRulesPath( const Json::Value &labDoc,
                                 const std::string &repoDataRoot )
{
  if ( !labDoc.isObject() || !labDoc.isMember( "grading_rules" )
       || !labDoc["grading_rules"].isString() )
    return {};
  const std::string ref = labDoc["grading_rules"].asString();
  if ( ref.empty() ) return {};

  const std::filesystem::path p( ref );
  if ( p.is_absolute() ) return ref;

  // Repo-relative ("data/labs/grading/x.rules.json") — the data root IS the
  // repo's data/ directory, so strip a leading "data/" and resolve under it.
  static constexpr const char *kDataPrefix = "data/";
  const std::string underData = ref.starts_with( kDataPrefix )
                                  ? ref.substr( std::string_view( kDataPrefix ).size() )
                                  : ref;
  std::error_code ec;
  const std::filesystem::path candidate = std::filesystem::path( repoDataRoot ) / underData;
  if ( !repoDataRoot.empty() && std::filesystem::exists( candidate, ec ) )
    return candidate.string();

  // Not found on disk: hand the ref to the grader, which applies its own
  // search order and refuses honestly when nothing resolves.
  return ref;
}

sicnu::teaching::LabFeedbackProjection projectValidation(
  const LabValidateInput &input )
{
  if ( input.artifactPath.empty() )
  {
    auto fb = sicnu::teaching::LabFeedbackProjection::fromReports(
      input.labId, Json::Value(), Json::Value() );
    fb.issuesZh.push_back( "未提供产物路径：技术验证与评分未执行（不确定，非通过）" );
    return fb;
  }

  sicnu::agent::OutputVerifier verifier;
  const Json::Value verifierLens =
    verifierLensJson( verifier.verify( QString::fromStdString( input.artifactPath ) ) );

  const QString rulesOrLab = input.rulesPath.empty()
                               ? QString::fromStdString( input.labId )
                               : QString::fromStdString( input.rulesPath );
  const auto grade = verifier.gradeForTeaching(
    rulesOrLab, QString::fromStdString( input.artifactPath ) );

  return sicnu::teaching::LabFeedbackProjection::fromReports(
    input.labId, verifierLens, grade.toBodyJson() );
}

} // namespace sicnu::app::teaching
