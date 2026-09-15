// rs_geometry_validity.cpp — see rs_geometry_validity.h.
#include "rs_geometry_validity.h"

#include <qgsgeometry.h>

QVector<RsValidityIssue> RsGeometryValidity::validate( const QgsGeometry &geometry,
                                                       Qgis::GeometryValidationEngine engine )
{
    QVector<RsValidityIssue> issues;
    if ( geometry.isNull() )
    {
        RsValidityIssue nullIssue;
        nullIssue.message = QStringLiteral( "null geometry" );
        issues.append( nullIssue );
        return issues;
    }

    QVector<QgsGeometry::Error> errors;
    geometry.validateGeometry( errors, engine );
    issues.reserve( errors.size() );
    for ( const QgsGeometry::Error &e : errors )
    {
        RsValidityIssue issue;
        issue.message = e.what();
        issue.hasLocation = e.hasWhere();
        if ( e.hasWhere() )
        {
            issue.x = e.where().x();
            issue.y = e.where().y();
        }
        issues.append( issue );
    }
    return issues;
}

bool RsGeometryValidity::isValid( const QgsGeometry &geometry, Qgis::GeometryValidationEngine engine )
{
    return validate( geometry, engine ).isEmpty();
}

QgsGeometry RsGeometryValidity::repairPreview( const QgsGeometry &geometry,
                                               bool keepCollapsed,
                                               bool *ok,
                                               Qgis::MakeValidMethod method )
{
    if ( ok )
        *ok = false;
    if ( geometry.isNull() )
        return QgsGeometry();

    const QgsGeometry repaired = geometry.makeValid( method, keepCollapsed );
    if ( repaired.isNull() )
        return QgsGeometry();
    if ( ok )
        *ok = true;
    return repaired;
}
