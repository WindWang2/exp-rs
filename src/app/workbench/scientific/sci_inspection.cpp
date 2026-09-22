/***************************************************************************
 * sci_inspection.cpp — versioned codec for ScientificInspection (ADR 0172)
 *
 * QJson objects serialize with sorted keys, so the same value always renders
 * the same bytes — the determinism the agent replay contract needs.
 ***************************************************************************/
#include "app/workbench/scientific/sci_inspection.h"

#include <QJsonArray>
#include <QJsonObject>

#include <optional>

namespace sicnu::app::sci
{
namespace
{

QString severityToWire( sicnu::data::DiagnosticSeverity severity )
{
  switch ( severity )
  {
    case sicnu::data::DiagnosticSeverity::Warning:
      return QStringLiteral( "warning" );
    case sicnu::data::DiagnosticSeverity::Error:
      return QStringLiteral( "error" );
    case sicnu::data::DiagnosticSeverity::Info:
      break;
  }
  return QStringLiteral( "info" );
}

std::optional<sicnu::data::DiagnosticSeverity> severityFromWire( const QString &text )
{
  if ( text == QLatin1String( "info" ) )
    return sicnu::data::DiagnosticSeverity::Info;
  if ( text == QLatin1String( "warning" ) )
    return sicnu::data::DiagnosticSeverity::Warning;
  if ( text == QLatin1String( "error" ) )
    return sicnu::data::DiagnosticSeverity::Error;
  return std::nullopt;
}

QJsonObject factToJson( const SciFact &fact )
{
  QJsonObject json;
  json.insert( QStringLiteral( "key" ), fact.key );
  json.insert( QStringLiteral( "label" ), fact.label );
  json.insert( QStringLiteral( "value" ), fact.value );
  json.insert( QStringLiteral( "status" ), factStatusToWire( fact.status ) );
  json.insert( QStringLiteral( "source" ), fact.source );
  json.insert( QStringLiteral( "detail" ), fact.detail );
  json.insert( QStringLiteral( "explanation" ), fact.explanation );
  return json;
}

QJsonObject findingToJson( const SciFinding &finding )
{
  QJsonObject json;
  json.insert( QStringLiteral( "code" ), finding.code );
  json.insert( QStringLiteral( "severity" ), severityToWire( finding.severity ) );
  json.insert( QStringLiteral( "message" ), finding.message );
  json.insert( QStringLiteral( "evidence" ), finding.evidence );
  return json;
}

QJsonObject sectionToJson( const SciSectionReport &section )
{
  QJsonObject json;
  json.insert( QStringLiteral( "id" ), section.id );
  json.insert( QStringLiteral( "title" ), section.title );
  json.insert( QStringLiteral( "available" ), section.available );
  json.insert( QStringLiteral( "unavailable_reason" ), section.unavailableReason );
  QJsonArray facts;
  for ( const SciFact &fact : section.facts )
    facts.append( factToJson( fact ) );
  json.insert( QStringLiteral( "facts" ), facts );
  QJsonArray findings;
  for ( const SciFinding &finding : section.findings )
    findings.append( findingToJson( finding ) );
  json.insert( QStringLiteral( "findings" ), findings );
  json.insert( QStringLiteral( "truncated" ), section.truncated );
  json.insert( QStringLiteral( "truncation_note" ), section.truncationNote );
  return json;
}

sicnu::data::Diagnostic codecError( const QString &code, const QString &message )
{
  sicnu::data::Diagnostic diagnostic;
  diagnostic.code = code;
  diagnostic.message = message;
  diagnostic.severity = sicnu::data::DiagnosticSeverity::Error;
  return diagnostic;
}

} // namespace

QString factStatusToWire( FactStatus status )
{
  switch ( status )
  {
    case FactStatus::Observed:
      return QStringLiteral( "observed" );
    case FactStatus::Inferred:
      return QStringLiteral( "inferred" );
    case FactStatus::Assumed:
      return QStringLiteral( "assumed" );
    case FactStatus::Unknown:
      return QStringLiteral( "unknown" );
    case FactStatus::Conflict:
      return QStringLiteral( "conflict" );
  }
  return QStringLiteral( "unknown" );
}

FactStatus factStatusFromAuthoritative( const QString &status )
{
  const std::optional<FactStatus> parsed = factStatusFromWireStrict( status );
  if ( parsed )
    return *parsed;
  // Harness vocabulary rename: a "derived" fact (closed documented rule) is
  // presented as Inferred.
  if ( status == QLatin1String( "derived" ) )
    return FactStatus::Inferred;
  return FactStatus::Unknown;
}

std::optional<FactStatus> factStatusFromWireStrict( const QString &status )
{
  if ( status == QLatin1String( "observed" ) )
    return FactStatus::Observed;
  if ( status == QLatin1String( "inferred" ) )
    return FactStatus::Inferred;
  if ( status == QLatin1String( "assumed" ) )
    return FactStatus::Assumed;
  if ( status == QLatin1String( "unknown" ) )
    return FactStatus::Unknown;
  if ( status == QLatin1String( "conflict" ) )
    return FactStatus::Conflict;
  return std::nullopt;
}

QJsonDocument inspectionToJson( const ScientificInspection &inspection )
{
  QJsonObject root;
  root.insert( QStringLiteral( "kind" ), QString::fromLatin1( kSciInspectionKind ) );
  root.insert( QStringLiteral( "schema_version" ),
               inspection.schemaVersion.isEmpty()
                 ? QString::fromLatin1( kSciInspectionSchemaVersion )
                 : inspection.schemaVersion );
  const QDateTime utc = inspection.generatedAtUtc.toUTC();
  root.insert( QStringLiteral( "generated_at_utc" ),
               utc.isValid() ? utc.toString( Qt::ISODateWithMs ) : QString() );

  QJsonObject target;
  target.insert( QStringLiteral( "kind" ), inspection.target.kind );
  target.insert( QStringLiteral( "id" ), inspection.target.id );
  target.insert( QStringLiteral( "label" ), inspection.target.displayLabel );
  root.insert( QStringLiteral( "target" ), target );

  QJsonArray sections;
  for ( const SciSectionReport &section : inspection.sections )
    sections.append( sectionToJson( section ) );
  root.insert( QStringLiteral( "sections" ), sections );

  return QJsonDocument( root );
}

sicnu::data::Result<ScientificInspection> inspectionFromJson( const QJsonDocument &doc )
{
  const QJsonObject root = doc.object();
  if ( !doc.isObject() || root.isEmpty() )
    return sicnu::data::Result<ScientificInspection>::failure(
      codecError( QStringLiteral( "sci.codec.envelope" ),
                  QStringLiteral( "sci_inspection 文档必须是 JSON 对象" ) ) );

  const QString kind = root.value( QLatin1String( "kind" ) ).toString();
  if ( kind != QString::fromLatin1( kSciInspectionKind ) )
    return sicnu::data::Result<ScientificInspection>::failure(
      codecError( QStringLiteral( "sci.codec.kind" ),
                  QStringLiteral( "未知文档 kind: %1" ).arg( kind ) ) );

  const QString version = root.value( QLatin1String( "schema_version" ) ).toString();
  if ( version.isEmpty() )
    return sicnu::data::Result<ScientificInspection>::failure(
      codecError( QStringLiteral( "sci.codec.schema" ),
                  QStringLiteral( "缺少 schema_version" ) ) );
  if ( version != QString::fromLatin1( kSciInspectionSchemaVersion ) )
    return sicnu::data::Result<ScientificInspection>::failure(
      codecError( QStringLiteral( "sci.codec.version" ),
                  QStringLiteral( "不支持的 sci_inspection 版本: %1" ).arg( version ) ) );

  const QDateTime generated =
    QDateTime::fromString( root.value( QLatin1String( "generated_at_utc" ) ).toString(),
                           Qt::ISODateWithMs );
  if ( !generated.isValid() )
    return sicnu::data::Result<ScientificInspection>::failure(
      codecError( QStringLiteral( "sci.codec.timestamp" ),
                  QStringLiteral( "generated_at_utc 不是可解析的 ISO 时间戳" ) ) );

  const QJsonObject targetJson = root.value( QLatin1String( "target" ) ).toObject();
  if ( !targetJson.contains( QLatin1String( "kind" ) )
       || !targetJson.contains( QLatin1String( "id" ) ) )
    return sicnu::data::Result<ScientificInspection>::failure(
      codecError( QStringLiteral( "sci.codec.target" ),
                  QStringLiteral( "target 缺少 kind/id" ) ) );

  const QJsonValue sectionsValue = root.value( QLatin1String( "sections" ) );
  if ( !sectionsValue.isArray() )
    return sicnu::data::Result<ScientificInspection>::failure(
      codecError( QStringLiteral( "sci.codec.sections" ),
                  QStringLiteral( "sections 必须是数组" ) ) );
  const QJsonArray sectionsJson = sectionsValue.toArray();

  ScientificInspection inspection;
  inspection.schemaVersion = version;
  inspection.generatedAtUtc = generated.toUTC();
  inspection.target.kind = targetJson.value( QLatin1String( "kind" ) ).toString();
  inspection.target.id = targetJson.value( QLatin1String( "id" ) ).toString();
  inspection.target.displayLabel = targetJson.value( QLatin1String( "label" ) ).toString();

  for ( const QJsonValue &sectionValue : sectionsJson )
  {
    const QJsonObject sectionJson = sectionValue.toObject();
    if ( sectionJson.isEmpty() )
      return sicnu::data::Result<ScientificInspection>::failure(
        codecError( QStringLiteral( "sci.codec.sections" ),
                    QStringLiteral( "section 必须是对象" ) ) );

    SciSectionReport section;
    section.id = sectionJson.value( QLatin1String( "id" ) ).toString();
    section.title = sectionJson.value( QLatin1String( "title" ) ).toString();
    section.available = sectionJson.value( QLatin1String( "available" ) ).toBool( true );
    section.unavailableReason =
      sectionJson.value( QLatin1String( "unavailable_reason" ) ).toString();
    section.truncated = sectionJson.value( QLatin1String( "truncated" ) ).toBool();
    section.truncationNote = sectionJson.value( QLatin1String( "truncation_note" ) ).toString();

    const QJsonArray factsJson = sectionJson.value( QLatin1String( "facts" ) ).toArray();
    for ( const QJsonValue &factValue : factsJson )
    {
      const QJsonObject factJson = factValue.toObject();
      const QString statusText = factJson.value( QLatin1String( "status" ) ).toString();
      const std::optional<FactStatus> status = factStatusFromWireStrict( statusText );
      if ( statusText.isEmpty() || !status )
        return sicnu::data::Result<ScientificInspection>::failure(
          codecError( QStringLiteral( "sci.codec.status" ),
                      QStringLiteral( "fact %1 带未知状态 %2" )
                        .arg( factJson.value( QLatin1String( "key" ) ).toString(), statusText ) ) );
      SciFact fact;
      fact.key = factJson.value( QLatin1String( "key" ) ).toString();
      fact.label = factJson.value( QLatin1String( "label" ) ).toString();
      fact.value = factJson.value( QLatin1String( "value" ) ).toString();
      fact.status = *status;
      fact.source = factJson.value( QLatin1String( "source" ) ).toString();
      fact.detail = factJson.value( QLatin1String( "detail" ) ).toString();
      fact.explanation = factJson.value( QLatin1String( "explanation" ) ).toString();
      section.facts.push_back( fact );
    }

    const QJsonArray findingsJson = sectionJson.value( QLatin1String( "findings" ) ).toArray();
    for ( const QJsonValue &findingValue : findingsJson )
    {
      const QJsonObject findingJson = findingValue.toObject();
      const QString severityText = findingJson.value( QLatin1String( "severity" ) ).toString();
      const std::optional<sicnu::data::DiagnosticSeverity> severity =
        severityFromWire( severityText );
      if ( severityText.isEmpty() || !severity )
        return sicnu::data::Result<ScientificInspection>::failure(
          codecError( QStringLiteral( "sci.codec.severity" ),
                      QStringLiteral( "finding 带未知严重级别 %1" ).arg( severityText ) ) );
      SciFinding finding;
      finding.code = findingJson.value( QLatin1String( "code" ) ).toString();
      finding.severity = *severity;
      finding.message = findingJson.value( QLatin1String( "message" ) ).toString();
      finding.evidence = findingJson.value( QLatin1String( "evidence" ) ).toString();
      section.findings.push_back( finding );
    }

    inspection.sections.push_back( std::move( section ) );
  }

  return sicnu::data::Result<ScientificInspection>::success( std::move( inspection ) );
}

} // namespace sicnu::app::sci
