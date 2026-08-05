#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "python/qgis_python.h"
#include "python/sicnu_python_runner.h"
#include "processing/framework/algorithm_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/json_params_converter.h"

#include <qgsapplication.h>
#include <qgsproject.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingprovider.h>
#include <processing/qgsprocessingalgorithm.h>
#include "processing/qgsprocessingparameters.h"

#include <QDir>
#include <QVariantMap>
#include <QDebug>

class TestCustomAlgorithm : public QgsProcessingAlgorithm
{
public:
    TestCustomAlgorithm() = default;
    ~TestCustomAlgorithm() override = default;

    QgsProcessingAlgorithm *createInstance() const override
    {
        return new TestCustomAlgorithm();
    }

    QString name() const override { return QStringLiteral( "test_custom_add" ); }
    QString displayName() const override { return QStringLiteral( "Test Custom Add Algorithm" ); }
    QString group() const override { return QStringLiteral( "Custom Tools" ); }
    QString groupId() const override { return QStringLiteral( "custom_tools" ); }
    QString shortDescription() const override { return QStringLiteral( "Adds two numbers" ); }

    void initAlgorithm( const QVariantMap &config = QVariantMap() ) override
    {
        Q_UNUSED( config );
        addParameter( new QgsProcessingParameterNumber( QStringLiteral( "A" ), QStringLiteral( "Input A" ), Qgis::ProcessingNumberParameterType::Integer, 0 ) );
        addParameter( new QgsProcessingParameterNumber( QStringLiteral( "B" ), QStringLiteral( "Input B" ), Qgis::ProcessingNumberParameterType::Integer, 0 ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        Q_UNUSED( feedback );
        int a = parameterAsInt( parameters, QStringLiteral( "A" ), context );
        int b = parameterAsInt( parameters, QStringLiteral( "B" ), context );

        QVariantMap results;
        results[QStringLiteral( "RESULT" )] = a + b;
        return results;
    }
};

class TestCustomProvider : public QgsProcessingProvider
{
public:
    TestCustomProvider() = default;
    ~TestCustomProvider() override = default;

    QString id() const override { return QStringLiteral( "test_custom_provider" ); }
    QString name() const override { return QStringLiteral( "Test Custom Provider" ); }

    void loadAlgorithms() override
    {
        addAlgorithm( new TestCustomAlgorithm() );
    }
};

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

TEST_CASE( "AlgorithmEngine listens to processingRegistry providerAdded signal", "[processing]" )
{
  sicnu::AlgorithmEngine::instance().initialize();

  // Dynamically add provider to QgsApplication processing registry
  auto *provider = new TestCustomProvider();
  QgsApplication::processingRegistry()->addProvider( provider );

  // Check if AtomicAlgorithmRegistry picked up 'test_custom_provider:test_custom_add'
  const QString targetAlgoId = QStringLiteral( "test_custom_provider:test_custom_add" );
  auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( targetAlgoId.toStdString() );
  REQUIRE( adapter != nullptr );
  CHECK( adapter->descriptor().id == targetAlgoId.toStdString() );
  CHECK( adapter->descriptor().displayName == "Test Custom Add Algorithm" );

  // Execute algorithm via AtomicAlgorithmAdapter
  QVariantMap params;
  params[QStringLiteral( "A" )] = 15;
  params[QStringLiteral( "B" )] = 27;

  Json::Value res = adapter->execute( sicnu::processing::variantToJsonValue( params ), nullptr );
  CHECK( !res.isMember( "error" ) );
}

TEST_CASE( "AlgorithmEngine interacts with Python engine", "[python][processing]" )
{
  REQUIRE( QgisPython::instance().initialize() );

  QString error;
  QString result;
  bool ok = QgisPython::instance().evalString( QStringLiteral( "15 + 27" ), result, error );
  CHECK( ok );
  CHECK( result == QStringLiteral( "42" ) );
}
