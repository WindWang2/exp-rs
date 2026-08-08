// src/app/widgets/raster_layer_combo.cpp — shared raster layer picker
#include "raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <qgsproject.h>

RasterLayerCombo::RasterLayerCombo( QWidget *parent )
  : QComboBox( parent )
{
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
