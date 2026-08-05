// rs_classifier_backend_factory.cpp — ADR 0061. See header for design notes.

#include "rs_classifier_backend_factory.h"

#include "rs_classifier_kmeans.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_svm.h"

std::unique_ptr<RsClassifierBackend> RsClassifierBackendFactory::create(
  const QString &methodName )
{
  const QString m = methodName.trimmed().toLower();
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
