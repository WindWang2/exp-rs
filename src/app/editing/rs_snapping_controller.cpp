// rs_snapping_controller.cpp — see rs_snapping_controller.h.
#include "rs_snapping_controller.h"

#include <qgsmapcanvas.h>
#include <qgssnappingconfig.h>
#include <qgssnappingutils.h>

RsSnappingController::RsSnappingController( QgsMapCanvas *canvas, QObject *parent )
  : QObject( parent )
  , mCanvas( canvas )
{
    if ( canvas )
    {
        mUtils = canvas->snappingUtils();
        connect( mUtils, &QgsSnappingUtils::configChanged, this, &RsSnappingController::snappingChanged );
    }
}

void RsSnappingController::enableVertexSegment( double tolerance, Qgis::MapToolUnit mapUnits, bool intersectionSnapping )
{
    if ( !mUtils )
        return;
    QgsSnappingConfig cfg = mUtils->config();
    cfg.setEnabled( true );
    cfg.setMode( Qgis::SnappingMode::ActiveLayer );
    cfg.setTypeFlag( Qgis::SnappingTypes( Qgis::SnappingType::Vertex ) | Qgis::SnappingType::Segment );
    cfg.setTolerance( tolerance );
    cfg.setUnits( mapUnits );
    cfg.setIntersectionSnapping( intersectionSnapping );
    mUtils->setConfig( cfg );
}

bool RsSnappingController::enabled() const
{
    return mUtils ? mUtils->config().enabled() : false;
}

Qgis::SnappingTypes RsSnappingController::types() const
{
    return mUtils ? mUtils->config().typeFlag() : Qgis::SnappingTypes();
}

double RsSnappingController::tolerance() const
{
    return mUtils ? mUtils->config().tolerance() : 0.0;
}

Qgis::MapToolUnit RsSnappingController::units() const
{
    return mUtils ? mUtils->config().units() : Qgis::MapToolUnit::Project;
}

Qgis::SnappingMode RsSnappingController::mode() const
{
    return mUtils ? mUtils->config().mode() : Qgis::SnappingMode::ActiveLayer;
}

bool RsSnappingController::intersectionSnapping() const
{
    return mUtils ? mUtils->config().intersectionSnapping() : false;
}
