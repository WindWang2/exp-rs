// rs_classifier_backend_factory.cpp — ADR 0061.
#include "rs_classifier_backend_factory.h"

#include "rs_classifier_isodata.h"
#include "rs_classifier_kmeans.h"
#include "rs_classifier_knn.h"
#include "rs_classifier_statistical.h"
#include "rs_classifier_mlp.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_random_forest.h"
#include "rs_classifier_svm.h"

std::unique_ptr<RsClassifierBackend> RsClassifierBackendFactory::create(
  const QString &methodName )
{
  return create( methodName, RsClassifierBackendParams{} );
}

std::unique_ptr<RsClassifierBackend> RsClassifierBackendFactory::create(
  const QString &methodName,
  const RsClassifierBackendParams &params )
{
  const QString m = methodName.trimmed().toLower();
  if ( m.contains( QStringLiteral( "mlp" ) ) || m.contains( QStringLiteral( "ann" ) ) || m.contains( QStringLiteral( "neural" ) ) )
    return std::make_unique<RsMlpBackend>( params.mlpHiddenLayerSize, params.mlpMaxIter );
  if ( m.contains( QStringLiteral( "rf" ) ) || m.contains( QStringLiteral( "forest" ) ) || m.contains( QStringLiteral( "rtrees" ) ) )
    return std::make_unique<RsRandomForestBackend>(
      params.rfNumTrees, params.rfMaxDepth, params.rfMinSampleCount );
  if ( m.contains( QStringLiteral( "bayes" ) ) )
    return std::make_unique<RsClassifierNormalBayes>();
  if ( m.contains( QStringLiteral( "isodata" ) ) )
    return std::make_unique<RsClassifierIsodata>();
  if ( m.contains( QStringLiteral( "kmeans" ) ) )
    return std::make_unique<RsClassifierKMeans>();
  if ( m.contains( QStringLiteral( "knn" ) ) || m.contains( QStringLiteral( "nearest" ) ) )
    return std::make_unique<RsClassifierKnn>();
  if ( m.contains( QStringLiteral( "mahalanobis" ) ) )
    return std::make_unique<RsClassifierMahalanobis>();
  if ( m.contains( QStringLiteral( "min_distance" ) ) || m.contains( QStringLiteral( "mindistance" ) ) || m.contains( QStringLiteral( "minimum" ) ) )
    return std::make_unique<RsClassifierMinDistance>();
  return std::make_unique<RsClassifierSvm>();
}

std::unique_ptr<RsClassifierBackend> RsClassifierBackendFactory::createKMeans( int k )
{
  return std::make_unique<RsClassifierKMeans>( k );
}

std::unique_ptr<RsClassifierBackend> RsClassifierBackendFactory::createIsodata(
  const RsClassifierIsodata::Params &params )
{
  return std::make_unique<RsClassifierIsodata>( params );
}
