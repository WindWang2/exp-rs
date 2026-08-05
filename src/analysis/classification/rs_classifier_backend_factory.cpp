// rs_classifier_backend_factory.cpp — ADR 0061.
#include "rs_classifier_backend_factory.h"

#include "rs_classifier_kmeans.h"
#include "rs_classifier_mlp.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_random_forest.h"
#include "rs_classifier_svm.h"

std::unique_ptr<RsClassifierBackend> RsClassifierBackendFactory::create(
  const QString &methodName )
{
  const QString m = methodName.trimmed().toLower();
  if ( m.contains( QStringLiteral( "mlp" ) ) || m.contains( QStringLiteral( "ann" ) ) || m.contains( QStringLiteral( "neural" ) ) )
    return std::make_unique<RsMlpBackend>();
  if ( m.contains( QStringLiteral( "rf" ) ) || m.contains( QStringLiteral( "forest" ) ) || m.contains( QStringLiteral( "rtrees" ) ) )
    return std::make_unique<RsRandomForestBackend>();
  if ( m.contains( QStringLiteral( "bayes" ) ) )
    return std::make_unique<RsClassifierNormalBayes>();
  if ( m.contains( QStringLiteral( "kmeans" ) ) )
    return std::make_unique<RsClassifierKMeans>();
  return std::make_unique<RsClassifierSvm>();
}

std::unique_ptr<RsClassifierBackend> RsClassifierBackendFactory::createKMeans( int k )
{
  return std::make_unique<RsClassifierKMeans>( k );
}
