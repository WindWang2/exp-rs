#include "measure_tool.h"
#include "core/sicnu_logging.h"

#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgsrubberband.h>
#include <qgsproject.h>
#include <qgspointxy.h>
#include <qgis.h>
#include <qgsunittypes.h>

#include <QMessageBox>
#include <QKeyEvent>

MeasureTool::MeasureTool( QgsMapCanvas *canvas, MeasureMode mode, QObject *parent )
    : QgsMapTool( canvas )
    , mMode( mode )
{
    Q_UNUSED( parent );

    SICNU_LOG_INFO( SicnuLogTags::MapTools, QString( "Measure tool created: mode=%1" )
        .arg( mode == Distance ? "Distance" : "Area" ) );

    mRubberBand = new QgsRubberBand( canvas,
        mMode == Area ? Qgis::GeometryType::Polygon : Qgis::GeometryType::Line );
    mRubberBand->setColor( QColor( 255, 0, 0, 180 ) );
    mRubberBand->setWidth( 2 );
    mRubberBand->show();

    // Configure distance area for geodesic calculations
    QgsProject *project = QgsProject::instance();
    mDistanceArea.setSourceCrs( project->crs(), project->transformContext() );
    mDistanceArea.setEllipsoid( project->ellipsoid() );
}

MeasureTool::~MeasureTool()
{
    reset();
    delete mRubberBand;
    mRubberBand = nullptr;
}

void MeasureTool::canvasPressEvent( QgsMapMouseEvent *e )
{
    if ( e->button() == Qt::RightButton )
    {
        finishMeasurement();
        return;
    }

    if ( e->button() == Qt::LeftButton )
    {
        QgsPointXY point = e->mapPoint();
        mPoints.append( point );
        mRubberBand->addPoint( point, true );
    }
}

void MeasureTool::canvasMoveEvent( QgsMapMouseEvent *e )
{
    if ( mPoints.isEmpty() )
        return;

    // Update the rubber band to follow the cursor
    if ( mMode == Area )
    {
        if ( mRubberBand->numberOfVertices() > mPoints.size() )
            mRubberBand->removeLastPoint( 0, false );
        mRubberBand->addPoint( e->mapPoint(), true );
    }
    else
    {
        if ( mRubberBand->numberOfVertices() > mPoints.size() )
            mRubberBand->movePoint( e->mapPoint() );
        else
            mRubberBand->addPoint( e->mapPoint(), true );
    }
}

void MeasureTool::canvasReleaseEvent( QgsMapMouseEvent *e )
{
    Q_UNUSED( e );
    // Points are added on press; nothing to do on release
}

void MeasureTool::canvasDoubleClickEvent( QgsMapMouseEvent *e )
{
    Q_UNUSED( e );
    finishMeasurement();
}

void MeasureTool::keyPressEvent( QKeyEvent *e )
{
    if ( e->key() == Qt::Key_Escape )
    {
        reset();
        return;
    }
    QgsMapTool::keyPressEvent( e );
}

void MeasureTool::activate()
{
    QgsMapTool::activate();
    reset();
}

void MeasureTool::deactivate()
{
    reset();
    QgsMapTool::deactivate();
}

void MeasureTool::finishMeasurement()
{
    if ( mPoints.size() < 2 )
    {
        reset();
        return;
    }

    SICNU_LOG_INFO( SicnuLogTags::MapTools, QString( "Finishing measurement: %1 points, mode=%2" )
        .arg( mPoints.size() ).arg( mMode == Distance ? "Distance" : "Area" ) );

    double value = 0;
    QString unit;

    if ( mMode == Distance )
    {
        value = mDistanceArea.measureLine( mPoints );
        unit = QgsUnitTypes::toAbbreviatedString( mDistanceArea.lengthUnits() );
    }
    else
    {
        if ( mPoints.size() < 3 )
        {
            reset();
            return;
        }
        value = mDistanceArea.measurePolygon( mPoints );
        unit = QgsUnitTypes::toAbbreviatedString( mDistanceArea.areaUnits() );
    }

    emit measurementComplete( value, unit );

    // Format and display result
    QString label;
    if ( mMode == Distance )
        label = tr( "Distance: %1 %2" ).arg( value, 0, 'f', 2 ).arg( unit );
    else
        label = tr( "Area: %1 %2" ).arg( value, 0, 'f', 2 ).arg( unit );

    QMessageBox::information( canvas(), tr( "Measurement Result" ), label );

    reset();
}

void MeasureTool::reset()
{
    mPoints.clear();
    if ( mRubberBand )
    {
        mRubberBand->reset( mMode == Area ? Qgis::GeometryType::Polygon : Qgis::GeometryType::Line );
    }
}
