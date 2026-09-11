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
/// grid (width/height/bands) against the real raster.
/// Returns: 0 = geometry agrees (or nothing recorded), 1 = mismatch
/// (evidence filled), 2 = wrong-typed recorded fields (sidecar malformed).
int gridMismatchEvidence( const QString &productPath, const Json::Value &prov,
                          std::string *evidenceOut )
{
  const Json::Value &output = prov["output"];
  if ( !output.isObject() )
    return 0; // nothing recorded — nothing to contradict (truthful-empty 8.0 docs)
  // Wrong-typed fields are MalformedSidecar evidence, never exceptions
  // (the "never throws" contract). Tri-state: -1 = wrong type, 0 = absent,
  // 1 = a real integer was read into *value.
  const auto typedInt = [ & ]( const char *key, int *value ) -> int {
    const Json::Value &v = output[key];
    if ( v.isNull() )
      return 0;
    if ( v.isIntegral() )
    {
      *value = v.asInt();
      return 1;
    }
    return -1;
  };
  GDALAllRegister();
  GDALDatasetH dataset = GDALOpen( productPath.toUtf8().constData(), GA_ReadOnly );
  if ( !dataset )
  {
    if ( evidenceOut )
      *evidenceOut = "output raster is no longer openable (deleted or unreadable since publication)";
    return 1;
  }
  const int realWidth = GDALGetRasterXSize( dataset );
  const int realHeight = GDALGetRasterYSize( dataset );
  const int realBands = GDALGetRasterCount( dataset );
  GDALClose( dataset );
  std::string evidence;
  int recorded = 0;
  bool typeError = false;
  const auto field = [ & ]( const char *key, int real ) -> bool {
    const int state = typedInt( key, &recorded );
    if ( state < 0 )
    {
      evidence = std::string( "recorded '" ) + key + "' is not a number";
      typeError = true;
      return true; // stop
    }
    if ( state > 0 && recorded != real )
    {
      evidence = std::string( "recorded " ) + key + " " + std::to_string( recorded )
                   + " but the product has " + std::to_string( real );
      return true;
    }
    return false;
  };
  const bool mismatch = field( "width", realWidth )
                          || ( evidence.empty() && field( "height", realHeight ) )
                          || ( evidence.empty() && field( "bands", realBands ) );
  if ( !mismatch )
    return 0;
  if ( evidenceOut )
    *evidenceOut = evidence + " — the sidecar does not describe this product";
  return typeError ? 2 : 1;
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
       || !prov["schema"].isString() || prov["schema"].asString() != "exp-rs-prov/1" )
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
  const Json::Value &model = prov.isMember( "model" ) && prov["model"].isObject()
                               ? prov["model"] : Json::Value( Json::nullValue );
  const auto stringField = [ & ]( const Json::Value &parent, const char *key ) {
    if ( parent.isMember( key ) && parent[key].isString() )
      return parent[key].asString();
    return std::string();
  };
  std::string mismatch;
  if ( !expectation.modelIdentityTag.empty() )
  {
    const std::string tag = model.isNull() ? std::string() : stringField( model, "identity_tag" );
    if ( tag != expectation.modelIdentityTag )
      mismatch = "identity tag '" + tag + "' does not match the expected '"
                   + expectation.modelIdentityTag + "'";
  }
  if ( mismatch.empty() && !expectation.modelContentDigest.empty() )
  {
    const std::string digest =
      model.isNull() ? std::string() : stringField( model, "content_digest" );
    if ( digest != expectation.modelContentDigest )
      mismatch = "content digest '" + digest + "' does not match the expected '"
                   + expectation.modelContentDigest + "'";
  }
  if ( mismatch.empty() && !expectation.backend.empty() )
  {
    const std::string backend =
      prov.isMember( "execution" ) && prov["execution"].isObject()
        ? stringField( prov["execution"], "backend" ) : std::string();
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
  std::string gridEvidence;
  const int gridState =
    gridMismatchEvidence( QString::fromStdString( outputPath ), prov, &gridEvidence );
  if ( gridState == 2 )
  {
    verdict.state = ProvenanceVerdict::State::MalformedSidecar;
    verdict.detail = gridEvidence;
    return verdict;
  }
  if ( gridState == 1 )
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
