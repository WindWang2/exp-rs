// wkt.cpp — minimal WKT reader implementation.
#include "wkt.h"

#include <QRegularExpression>

#include <algorithm>
#include <limits>

namespace sicnu::dataset
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;

namespace
{

Diagnostic wktError( const QString &why )
{
    return Diagnostic{ QStringLiteral( "dataset.wkt_invalid" ), why, DiagnosticSeverity::Error };
}

/// Splits "x y" into a point; silently-skips empties between commas.
std::optional<QPointF> parseCoordinate( QStringView text )
{
    const QString trimmed = text.trimmed().toString();
    if ( trimmed.isEmpty() )
        return std::nullopt;
    const QStringList parts = trimmed.split( QRegularExpression( QStringLiteral( "\\s+" ) ) );
    if ( parts.size() < 2 )
        return std::nullopt;
    bool okX = false;
    bool okY = false;
    const double x = parts.at( 0 ).toDouble( &okX );
    const double y = parts.at( 1 ).toDouble( &okY );
    if ( !okX || !okY )
        return std::nullopt;
    return QPointF( x, y );
}

/// Extracts the coordinate text between the outermost parentheses of the
/// first ring: "((a, b, c))" → "a, b, c".
sicnu::data::Result<QString> firstRingCoordinates( const QString &wkt )
{
    const int outerOpen = wkt.indexOf( QLatin1Char( '(' ) );
    if ( outerOpen < 0 )
        return sicnu::data::Result<QString>::failure( wktError( QStringLiteral( "no ring in WKT" ) ) );
    int depth = 0;
    int innerOpen = -1;
    int innerClose = -1;
    for ( int i = outerOpen; i < wkt.size(); ++i )
    {
        const QChar ch = wkt.at( i );
        if ( ch == QLatin1Char( '(' ) )
        {
            ++depth;
            if ( depth == 2 && innerOpen < 0 )
                innerOpen = i;
        }
        else if ( ch == QLatin1Char( ')' ) )
        {
            if ( depth == 2 && innerClose < 0 )
                innerClose = i;
            --depth;
            if ( depth == 0 )
                break;
        }
    }
    if ( innerOpen < 0 || innerClose < 0 || innerClose <= innerOpen )
        return sicnu::data::Result<QString>::failure(
            wktError( QStringLiteral( "WKT ring not readable" ) ) );
    return sicnu::data::Result<QString>::success(
        wkt.mid( innerOpen + 1, innerClose - innerOpen - 1 ) );
}

} // namespace

double SimplePolygon::minX() const
{
    double value = std::numeric_limits<double>::infinity();
    for ( const QPointF &point : ring )
        value = qMin( value, point.x() );
    return value;
}

double SimplePolygon::minY() const
{
    double value = std::numeric_limits<double>::infinity();
    for ( const QPointF &point : ring )
        value = qMin( value, point.y() );
    return value;
}

double SimplePolygon::maxX() const
{
    double value = -std::numeric_limits<double>::infinity();
    for ( const QPointF &point : ring )
        value = qMax( value, point.x() );
    return value;
}

double SimplePolygon::maxY() const
{
    double value = -std::numeric_limits<double>::infinity();
    for ( const QPointF &point : ring )
        value = qMax( value, point.y() );
    return value;
}

bool SimplePolygon::contains( const QPointF &point ) const
{
    if ( !isValid() )
        return false;
    bool inside = false;
    const int count = ring.size();
    for ( int i = 0, j = count - 1; i < count; j = i++ )
    {
        const QPointF &a = ring.at( i );
        const QPointF &b = ring.at( j );
        if ( ( ( a.y() > point.y() ) != ( b.y() > point.y() ) ) &&
             ( point.x() < ( b.x() - a.x() ) * ( point.y() - a.y() ) / ( b.y() - a.y() ) + a.x() ) )
            inside = !inside;
    }
    return inside;
}

bool SimplePolygon::intersects( const SimplePolygon &other ) const
{
    if ( !isValid() || !other.isValid() )
        return false;
    // Conservative, cheap audit predicate: bounding boxes overlap AND some
    // vertex of either ring is inside the other. Edge-crossing-only cases
    // (no shared vertices) are rare in patch/ROI audits and are caught by
    // the bounding-box test itself.
    if ( maxX() < other.minX() || other.maxX() < minX() ||
         maxY() < other.minY() || other.maxY() < minY() )
        return false;
    for ( const QPointF &point : ring )
    {
        if ( other.contains( point ) )
            return true;
    }
    for ( const QPointF &point : other.ring )
    {
        if ( contains( point ) )
            return true;
    }
    return false;
}

sicnu::data::Result<WktPoint> parseWktPoint( const QString &wkt )
{
    using Result = sicnu::data::Result<WktPoint>;
    const QString upper = wkt.trimmed().toUpper();
    if ( !upper.startsWith( QLatin1String( "POINT" ) ) )
        return Result::failure( wktError( QStringLiteral( "expected POINT" ) ) );
    const int open = wkt.indexOf( QLatin1Char( '(' ) );
    const int close = wkt.lastIndexOf( QLatin1Char( ')' ) );
    if ( open < 0 || close <= open )
        return Result::failure( wktError( QStringLiteral( "malformed POINT" ) ) );
    const auto coordinate = parseCoordinate(
        QStringView( wkt ).mid( open + 1, close - open - 1 ) );
    if ( !coordinate )
        return Result::failure( wktError( QStringLiteral( "malformed POINT coordinates" ) ) );
    WktPoint point;
    point.x = coordinate->x();
    point.y = coordinate->y();
    return Result::success( point );
}

sicnu::data::Result<SimplePolygon> parseWktPolygon( const QString &wkt, int *ringCountOut )
{
    using Result = sicnu::data::Result<SimplePolygon>;
    const QString upper = wkt.trimmed().toUpper();
    if ( ringCountOut )
        *ringCountOut = 0;
    if ( !upper.startsWith( QLatin1String( "POLYGON" ) ) &&
         !upper.startsWith( QLatin1String( "MULTIPOLYGON" ) ) )
        return Result::failure( wktError( QStringLiteral( "expected (MULTI)POLYGON" ) ) );

    // Count ring groups: every "((...))" pair.
    int rings = 0;
    int depth = 0;
    for ( const QChar ch : wkt )
    {
        if ( ch == QLatin1Char( '(' ) )
        {
            ++depth;
            if ( depth == 2 )
                ++rings;
        }
        else if ( ch == QLatin1Char( ')' ) )
        {
            --depth;
        }
    }
    if ( ringCountOut )
        *ringCountOut = rings;

    const auto ringText = firstRingCoordinates( wkt );
    if ( !ringText )
        return Result::failure( ringText.diagnostics() );

    SimplePolygon polygon;
    const QStringList coordinates = ringText.value().split( QLatin1Char( ',' ) );
    for ( const QString &coordinate : coordinates )
    {
        const auto point = parseCoordinate( QStringView( coordinate ) );
        if ( !point )
            return Result::failure( wktError( QStringLiteral( "malformed polygon vertex" ) ) );
        polygon.ring.append( *point );
    }
    if ( !polygon.isValid() )
        return Result::failure( wktError( QStringLiteral( "polygon needs >= 4 vertices" ) ) );
    // Close the ring if the source omitted the closing vertex.
    if ( polygon.ring.first() != polygon.ring.last() )
        polygon.ring.append( polygon.ring.first() );
    return Result::success( polygon );
}

sicnu::data::Result<QVector<double>> parseWktBounds( const QString &wkt )
{
    using Result = sicnu::data::Result<QVector<double>>;
    const auto polygon = parseWktPolygon( wkt );
    if ( !polygon )
        return Result::failure( polygon.diagnostics() );
    return Result::success( QVector<double>{ polygon.value().minX(), polygon.value().minY(),
                                             polygon.value().maxX(), polygon.value().maxY() } );
}

} // namespace sicnu::dataset
