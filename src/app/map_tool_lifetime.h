// map_tool_lifetime.h — destroy canvas-child map tools while the canvas lives
// (QgsMapCanvas qDeleteAll's scene items before ~QObject deletes child tools).
#pragma once

#include <qgsmapcanvas.h>
#include <qgsmaptool.h>
#include <qgsrubberband.h>

template<typename Tool>
inline void rsDestroyMapTool( Tool *&tool, QgsMapCanvas *canvas )
{
  if ( !tool )
    return;
  if ( canvas && canvas->mapTool() == tool )
    canvas->unsetMapTool( tool );
  tool->setParent( nullptr );
  delete tool;
  tool = nullptr;
}

/// Delete a scene-owned rubber band only while the canvas is still alive.
inline void rsDeleteRubberIfCanvasAlive( QgsRubberBand *&band, QgsMapCanvas *canvas )
{
  if ( canvas )
    delete band;
  band = nullptr;
}
