// src/app/widgets/raster_layer_combo.cpp — shared raster layer picker
#include "raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <qgsproject.h>

RasterLayerCombo::RasterLayerCombo( QWidget *parent )
  : QComboBox( parent )
{
  // F-01 (ui-backend-state-parity-r4): the combo used to be a one-shot
  // snapshot — layers added/removed while the host dialog stayed open (a
  // background task's auto-load, an import) left a stale picker behind. The
  // project layer set is the source of truth; track it.
  // Queued: the refresh must observe the project AFTER its mutation pass
  // finishes — re-entering QgsProject from inside layersAdded/Removed
  // delivery races the layer teardown (S3 stress caught the use-after-free).
  connect( QgsProject::instance(),
           qOverload<const QList<QgsMapLayer *> &>( &QgsProject::layersAdded ),
           this, [this]( const QList<QgsMapLayer *> & ) { refreshFromProject(); },
           Qt::QueuedConnection );
  connect( QgsProject::instance(), &QgsProject::layersRemoved,
           this, [this]( const QStringList & ) { refreshFromProject(); },
           Qt::QueuedConnection );
}

void RasterLayerCombo::populate()
{
  clear();
  const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
  for ( auto it = layers.constBegin(); it != layers.constEnd(); ++it )
  {
    auto *rasterLayer = qobject_cast<QgsRasterLayer *>( it.value() );
    if ( rasterLayer && rasterLayer->isValid() )
      addItem( rasterLayer->name(), rasterLayer->id() );
  }
}

void RasterLayerCombo::refreshFromProject()
{
  const QString selectedId = currentLayerId();
  // Suppress the transient -1 index churn while rebuilding: host dialogs
  // react to currentIndexChanged and must not observe a phantom empty state
  // between clear() and the re-selection.
  const QSignalBlocker blocker( this );
  populate();
  if ( !selectedId.isEmpty() )
    selectLayer( selectedId );
}

QString RasterLayerCombo::currentLayerId() const
{
  return currentData().toString();
}

QgsRasterLayer *RasterLayerCombo::currentRasterLayer() const
{
  const QString id = currentLayerId();
  if ( id.isEmpty() )
    return nullptr;
  return qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( id ) );
}

void RasterLayerCombo::selectLayer( const QString &id )
{
  const int index = findData( id );
  if ( index >= 0 )
    setCurrentIndex( index );
}
