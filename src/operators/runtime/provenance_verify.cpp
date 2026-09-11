// src/operators/runtime/provenance_verify.cpp — consumer-side provenance
// verification (see the header for the contract).
#include "operators/runtime/provenance_verify.h"

#include "operators/framework/rs_operator_error.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <json/json.h>

#include <memory>

namespace sicnu::operators::runtime {

namespace {

/// Opens the published product read-only and checks the recorded output
/// grid (width/height/bands) against the real raster. Returns an empty
/// string when the geometry agrees (or the sidecar records none), else the
/// mismatch evidence.
std::string gridMismatchEvidence( const QString &productPath, const Json::Value &prov )
{
  const Json::Value &output = prov["output"];
  if ( !output.isObject() )
    return {}; // nothing recorded — nothing to contradict (truthful-empty 8.0 docs)
  GDALAllRegister();
  GDALDatasetH dataset = GDALOpen( productPath.toUtf8().constData(), GA_ReadOnly );
  if ( !dataset )
    return "output raster is no longer openable (deleted or unreadable since publication)";
  const int realWidth = GDALGetRasterXSize( dataset );
  const int realHeight = GDALGetRasterYSize( dataset );
  const int realBands = GDALGetRasterCount( dataset );
  GDALClose( dataset );
  std::string evidence;
  if ( output.isMember( "width" ) && output["width"].asInt() != realWidth )
    evidence = "recorded width " + std::to_string( output["width"].asInt() )
                 + " but the product has " + std::to_string( realWidth );
  if ( evidence.empty() && output.isMember( "height" )
       && output["height"].asInt() != realHeight )
    evidence = "recorded height " + std::to_string( output["height"].asInt() )
                 + " but the product has " + std::to_string( realHeight );
  if ( evidence.empty() && output.isMember( "bands" ) && output["bands"].asInt() != realBands )
    evidence = "recorded band count " + std::to_string( output["bands"].asInt() )
                 + " but the product has " + std::to_string( realBands );
  if ( !evidence.empty() )
    return evidence + " — the sidecar does not describe this product";
  return {};
}

} // namespace

std::string provenanceSidecarPath( const std::string &outputPath )
{
  return outputPath + ".prov.json";
}

ProvenanceVerdict verifyProductProvenance( const std::string &outputPath,
                                           const ProvenanceExpectation &expectation )
{
  ProvenanceVerdict verdict;
  const QFileInfo product( QString::fromStdString( outputPath ) );
  if ( !product.exists() || !product.isFile() )
  {
    verdict.state = ProvenanceVerdict::State::ProductMissing;
    verdict.detail = "output product does not exist: " + outputPath;
    return verdict;
  }

  const QString sidecarPath = QString::fromStdString( provenanceSidecarPath( outputPath ) );
  const QFileInfo sidecar( sidecarPath );
  if ( !sidecar.exists() || !sidecar.isFile() )
  {
    // The 8.0 publish order guarantees this state means "crash between the
    // product rename and the sidecar rename" (or deliberate removal) — the
    // consumer must see it, not silently trust the product.
    verdict.state = ProvenanceVerdict::State::MissingSidecar;
    verdict.detail = "no provenance sidecar at " + sidecarPath.toStdString()
                       + " (publication did not complete or the sidecar was removed)";
    return verdict;
  }

  QFile sidecarFile( sidecarPath );
  if ( !sidecarFile.open( QIODevice::ReadOnly ) )
  {
    verdict.state = ProvenanceVerdict::State::MalformedSidecar;
    verdict.detail = "provenance sidecar is not readable: " + sidecarPath.toStdString();
    return verdict;
  }
  const QByteArray raw = sidecarFile.readAll();
  sidecarFile.close();

  Json::Value prov;
  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string parseErrors;
  if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &prov, &parseErrors ) )
  {
    verdict.state = ProvenanceVerdict::State::MalformedSidecar;
    verdict.detail = "provenance sidecar is not valid JSON: " + parseErrors;
    return verdict;
  }
  verdict.provenance = prov;

  if ( !prov.isObject() || !prov.isMember( "schema" )
       || prov["schema"].asString() != "exp-rs-prov/1" )
  {
    verdict.state = ProvenanceVerdict::State::UnsupportedSchema;
    verdict.detail = "provenance sidecar schema '"
                       + ( prov.isMember( "schema" ) && prov["schema"].isString()
                             ? prov["schema"].asString()
                             : std::string( "<absent>" ) )
                       + "' is not exp-rs-prov/1";
    return verdict;
  }

  // Expectation checks: model identity, digest, backend.
  const Json::Value &model = prov["model"];
  std::string mismatch;
  if ( !expectation.modelIdentityTag.empty() )
  {
    const std::string tag =
      model.isMember( "identity_tag" ) ? model["identity_tag"].asString() : std::string();
    if ( tag != expectation.modelIdentityTag )
      mismatch = "identity tag '" + tag + "' does not match the expected '"
                   + expectation.modelIdentityTag + "'";
  }
  if ( mismatch.empty() && !expectation.modelContentDigest.empty() )
  {
    const std::string digest =
      model.isMember( "content_digest" ) ? model["content_digest"].asString() : std::string();
    if ( digest != expectation.modelContentDigest )
      mismatch = "content digest '" + digest + "' does not match the expected '"
                   + expectation.modelContentDigest + "'";
  }
  if ( mismatch.empty() && !expectation.backend.empty() )
  {
    const Json::Value &execution = prov["execution"];
    const std::string backend =
      execution.isMember( "backend" ) ? execution["backend"].asString() : std::string();
    if ( backend != expectation.backend )
      mismatch = "backend '" + backend + "' does not match the expected '"
                   + expectation.backend + "'";
  }
  if ( !mismatch.empty() )
  {
    verdict.state = ProvenanceVerdict::State::ModelMismatch;
    verdict.detail = mismatch;
    return verdict;
  }

  // Grid consistency: the recorded output geometry must describe THIS file.
  const std::string gridEvidence = gridMismatchEvidence( QString::fromStdString( outputPath ), prov );
  if ( !gridEvidence.empty() )
  {
    verdict.state = ProvenanceVerdict::State::GridMismatch;
    verdict.detail = gridEvidence;
    return verdict;
  }

  // Staleness: the 8.0 publish order writes the sidecar AFTER the product,
  // so a product whose mtime is NEWER than its sidecar was rewritten without
  // a (complete) republication — exactly the stale-mismatched state the
  // write ordering promised never to leave behind silently.
  const QDateTime productTime = product.lastModified();
  const QDateTime sidecarTime = sidecar.lastModified();
  if ( productTime > sidecarTime )
  {
    verdict.state = ProvenanceVerdict::State::StaleProduct;
    verdict.detail = "product mtime (" + productTime.toString( Qt::ISODateWithMs ).toStdString()
                       + ") is newer than the sidecar ("
                       + sidecarTime.toString( Qt::ISODateWithMs ).toStdString()
                       + ") — the product changed after this provenance was written";
    return verdict;
  }

  verdict.state = ProvenanceVerdict::State::Ok;
  return verdict;
}

} // namespace sicnu::operators::runtime
