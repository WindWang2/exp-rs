#include <catch2/catch_test_macros.hpp>

#include "processing/qgsprocessingtoolboxtreeview.h"
#include <QApplication>
#include <QEvent>
#include <QPointer>

// The view already grants this fixture access to its paired model pointers.
class TestQgsProcessingModel
{
public:
  static QgsProcessingToolboxModel *toolboxModel( const QgsProcessingToolboxTreeView &view )
  {
    return view.mToolboxModel;
  }
};

TEST_CASE( "Toolbox replacement retains the new source after deferred deletion",
           "[processing][toolbox][regression]" )
{
  static int argc = 1;
  static char name[] = "test_processing_toolbox_replacement";
  static char *argv[] = { name, nullptr };
  static QApplication app( argc, argv );

  QgsProcessingToolboxTreeView view;
  QPointer<QAbstractItemModel> oldProxy = view.model();
  auto *replacement = new QgsProcessingToolboxProxyModel( &view );
  QPointer<QgsProcessingToolboxModel> source = replacement->toolboxModel();
  view.setToolboxProxyModel( replacement );
  REQUIRE( view.model() == replacement );
  REQUIRE( TestQgsProcessingModel::toolboxModel( view ) == source.data() );
  QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  CHECK( oldProxy.isNull() );
  REQUIRE( !source.isNull() );
  CHECK( TestQgsProcessingModel::toolboxModel( view ) == source.data() );
  // Reassigning the active model must not schedule it for destruction.
  view.setToolboxProxyModel( replacement );
  QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  REQUIRE( !source.isNull() );
  view.setFilterString( QStringLiteral( "missing algorithm" ) );
  CHECK( view.selectedAlgorithm() == nullptr );
}
