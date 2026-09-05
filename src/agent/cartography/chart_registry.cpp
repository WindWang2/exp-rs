// src/agent/cartography/chart_registry.cpp
#include "chart_registry.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <qgsbarchartplot.h>
#include <qgslayoutitemchart.h>
#include <qgslinechartplot.h>
#include <qgsplot.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sicnu::agent::cartography {

namespace {

const char *const kChartKinds[] = { "bar", "line", "pie", "histogram", "area", "scatter" };

bool isKnownKind( const std::string &kind )
{
  for ( const char *k : kChartKinds )
    if ( kind == k )
      return true;
  return false;
}

const QColor kPalette[] = {
  QColor( 0x4c, 0x78, 0xa8 ), QColor( 0xf2, 0x8e, 0x2b ), QColor( 0xe1, 0x57, 0x59 ),
  QColor( 0x76, 0xb7, 0xb2 ), QColor( 0x59, 0xa1, 0x4f ), QColor( 0xed, 0xc9, 0x48 ),
  QColor( 0xaf, 0x7b, 0xa1 ), QColor( 0xff, 0x9d, 0xa7 ), QColor( 0x9c, 0x75, 0x5f ),
  QColor( 0xb0, 0xb0, 0xb0 ),
};

QColor paletteColor( const Json::Value &style, int index )
{
  if ( style.isObject() && style.isMember( "palette" ) && style["palette"].isArray() &&
       index < static_cast<int>( style["palette"].size() ) &&
       QColor::isValidColor( QString::fromStdString( style["palette"][index].asString() ) ) )
  {
    return QColor( QString::fromStdString( style["palette"][index].asString() ) );
  }
  return kPalette[index % ( sizeof( kPalette ) / sizeof( kPalette[0] ) )];
}

std::vector<std::pair<QString, double>> inlineData( const Json::Value &chart, bool *ok )
{
  std::vector<std::pair<QString, double>> points;
  const Json::Value &binding = chart["binding"];
  if ( !binding.isObject() || !binding.isMember( "data" ) || !binding["data"].isArray() )
  {
    *ok = false;
    return points;
  }
  for ( const auto &entry : binding["data"] )
  {
    if ( !entry.isObject() )
      continue;
    const QString label = QString::fromStdString( entry.get( "label", "" ).asString() );
    const double value = entry.isMember( "value" ) && entry["value"].isNumeric()
                           ? entry["value"].asDouble()
                           : entry.isMember( "y" ) && entry["y"].isNumeric() ? entry["y"].asDouble()
                                                                             : 0.0;
    points.emplace_back( label, value );
  }
  *ok = true;
  return points;
}

void drawAxes( QPainter &painter, const QRectF &plotRect, double maxValue )
{
  painter.setPen( QPen( QColor( 0x90, 0x90, 0x90 ), 1 ) );
  painter.drawLine( plotRect.bottomLeft(), plotRect.bottomRight() );
  painter.drawLine( plotRect.bottomLeft(), plotRect.topLeft() );
  for ( int tick = 0; tick <= 4; ++tick )
  {
    const double y = plotRect.bottom() - plotRect.height() * tick / 4.0;
    painter.drawLine( QPointF( plotRect.left(), y ), QPointF( plotRect.left() - 4, y ) );
    painter.drawText( QRectF( 0, y - 8, plotRect.left() - 8, 16 ), Qt::AlignRight | Qt::AlignVCenter,
                      QString::number( maxValue * tick / 4.0, 'g', 3 ) );
  }
}

/// Shared two-value scale for the painter renderer.
bool renderInlineChart( const Json::Value &chart, QPainter &painter, const QSizeF &size,
                        QString *error )
{
  bool dataOk = false;
  const std::vector<std::pair<QString, double>> points = inlineData( chart, &dataOk );
  if ( !dataOk || points.empty() )
  {
    if ( error )
      *error = QStringLiteral( "inline chart needs binding.data with at least one entry" );
    return false;
  }

  const std::string kind = chart.get( "kind", "bar" ).asString();
  const QString title = QString::fromStdString( chart.get( "title", "" ).asString() );

  double maxValue = 0.0;
  for ( const auto &[ label, value ] : points )
  {
    Q_UNUSED( label );
    maxValue = std::max( maxValue, std::fabs( value ) );
  }
  if ( maxValue <= 0 )
    maxValue = 1.0;

  QFont font = painter.font();
  int fontPt = 10;
  if ( chart.isMember( "style" ) && chart["style"].isMember( "font_pt" ) &&
       chart["style"]["font_pt"].isNumeric() )
    fontPt = std::clamp( chart["style"]["font_pt"].asInt(), 6, 36 );
  font.setPointSizeF( fontPt );
  painter.setFont( font );

  QRectF plotRect( 44.0, 30.0, size.width() - 54.0, size.height() - 56.0 );

  if ( kind == "pie" )
  {
    double startAngle = 90.0 * 16;
    const double total = std::accumulate( points.begin(), points.end(), 0.0,
                                          []( double acc, const auto &p ) { return acc + p.second; } );
    if ( total <= 0 )
    {
      if ( error )
        *error = QStringLiteral( "pie chart needs positive values" );
      return false;
    }
    for ( int i = 0; i < static_cast<int>( points.size() ); ++i )
    {
      const double span = 360.0 * 16 * points[i].second / total;
      painter.setBrush( paletteColor( chart["style"], i ) );
      painter.setPen( Qt::white );
      painter.drawPie( plotRect, static_cast<int>( startAngle ), static_cast<int>( -span ) );
      startAngle -= span;
    }
  }
  else if ( kind == "bar" || kind == "histogram" )
  {
    drawAxes( painter, plotRect, maxValue );
    const int n = static_cast<int>( points.size() );
    const double barWidth = plotRect.width() / std::max( 1, n );
    for ( int i = 0; i < n; ++i )
    {
      const double h = plotRect.height() * std::fabs( points[i].second ) / maxValue;
      const QRectF bar( plotRect.left() + i * barWidth + 1, plotRect.bottom() - h,
                        std::max( 2.0, barWidth - 2.0 ), h );
      painter.setBrush( paletteColor( chart["style"], i ) );
      painter.setPen( Qt::NoPen );
      painter.drawRect( bar );
      if ( !points[i].first.isEmpty() && barWidth > 12 )
      {
        painter.setPen( QColor( 0x40, 0x40, 0x40 ) );
        painter.drawText( QRectF( plotRect.left() + i * barWidth, plotRect.bottom() + 4, barWidth, 18 ),
                          Qt::AlignCenter, points[i].first );
      }
    }
  }
  else // line / area / scatter
  {
    drawAxes( painter, plotRect, maxValue );
    const int n = static_cast<int>( points.size() );
    const double stepX = n > 1 ? plotRect.width() / ( n - 1 ) : 0.0;
    QPainterPath path;
    for ( int i = 0; i < n; ++i )
    {
      const double x = plotRect.left() + i * stepX;
      const double y = plotRect.bottom() - plotRect.height() * std::fabs( points[i].second ) / maxValue;
      if ( i == 0 )
        path.moveTo( x, y );
      else
        path.lineTo( x, y );
    }
    painter.setRenderHint( QPainter::Antialiasing, true );
    if ( kind == "area" )
    {
      QPainterPath filled = path;
      filled.lineTo( plotRect.right(), plotRect.bottom() );
      filled.lineTo( plotRect.left(), plotRect.bottom() );
      painter.fillPath( filled, paletteColor( chart["style"], 0 ) );
    }
    painter.setPen( QPen( paletteColor( chart["style"], 0 ), 2 ) );
    if ( kind == "scatter" )
    {
      for ( int i = 0; i < n; ++i )
      {
        const double x = plotRect.left() + i * stepX;
        const double y =
          plotRect.bottom() - plotRect.height() * std::fabs( points[i].second ) / maxValue;
        painter.drawEllipse( QPointF( x, y ), 3, 3 );
      }
    }
    else
    {
      painter.drawPath( path );
    }
  }

  if ( !title.isEmpty() )
  {
    painter.setPen( QColor( 0x20, 0x20, 0x20 ) );
    painter.drawText( QRectF( 0, 4, size.width(), 24 ), Qt::AlignCenter, title );
  }
  return true;
}

} // namespace

std::vector<std::string> validateChartSpec( const Json::Value &chart )
{
  std::vector<std::string> problems;
  if ( !chart.isObject() )
  {
    problems.push_back( "chart must be an object" );
    return problems;
  }
  const std::string kind = chart.isMember( "kind" ) && chart["kind"].isString()
                             ? chart["kind"].asString()
                             : "";
  if ( !isKnownKind( kind ) )
    problems.push_back( "kind must be one of bar|line|pie|histogram|area|scatter" );
  if ( !chart.isMember( "binding" ) || !chart["binding"].isObject() )
  {
    problems.push_back( "chart needs a binding object" );
    return problems;
  }
  const Json::Value &binding = chart["binding"];
  const std::string mode = binding.isMember( "mode" ) && binding["mode"].isString()
                             ? binding["mode"].asString()
                             : "inline";
  if ( mode != "inline" && mode != "vector_expression" )
    problems.push_back( "binding.mode must be inline|vector_expression" );
  if ( mode == "inline" )
  {
    if ( !binding.isMember( "data" ) || !binding["data"].isArray() || binding["data"].empty() )
      problems.push_back( "inline binding needs non-empty data array" );
    else if ( binding["data"].size() > 256 )
      problems.push_back( "inline binding capped at 256 data points" );
  }
  else
  {
    if ( !binding.isMember( "layer" ) || !binding["layer"].isString() )
      problems.push_back( "vector_expression binding needs 'layer'" );
    if ( !binding.isMember( "y_expression" ) || !binding["y_expression"].isString() )
      problems.push_back( "vector_expression binding needs 'y_expression'" );
  }
  return problems;
}

ChartRegistry &ChartRegistry::instance()
{
  static ChartRegistry registry;
  return registry;
}

QString ChartRegistry::createChart( Json::Value spec, QString *error )
{
  if ( !spec.isObject() )
    spec = Json::Value( Json::objectValue );
  // Idempotent id assignment (chart-N).
  Json::Value idHolder( Json::objectValue );
  idHolder["kind"] = spec.get( "kind", "" );
  const auto problems = validateChartSpec( spec );
  if ( !problems.empty() )
  {
    if ( error )
      *error = QString::fromStdString( problems.front() );
    return QString();
  }
  QMutexLocker lock( &mMutex );
  int ordinal = mCharts.size() + 1;
  const QString prefix = QStringLiteral( "chart-" );
  while ( mCharts.contains( prefix + QString::number( ordinal ) ) )
    ++ordinal;
  const QString id = prefix + QString::number( ordinal );
  spec["id"] = id.toStdString();
  mCharts.insert( id, spec );
  return id;
}

bool ChartRegistry::updateChart( const QString &id, const Json::Value &patch, QString *error )
{
  QMutexLocker lock( &mMutex );
  auto it = mCharts.find( id );
  if ( it == mCharts.end() )
  {
    if ( error )
      *error = QStringLiteral( "unknown chart '%1'" ).arg( id );
    return false;
  }
  if ( !patch.isObject() )
    return true;
  Json::Value updated = it.value();
  for ( const auto &key : patch.getMemberNames() )
  {
    if ( key == "id" )
      continue;
    updated[key] = patch[key];
  }
  const auto problems = validateChartSpec( updated );
  if ( !problems.empty() )
  {
    if ( error )
      *error = QString::fromStdString( problems.front() );
    return false;
  }
  it.value() = updated;
  return true;
}

bool ChartRegistry::removeChart( const QString &id )
{
  QMutexLocker lock( &mMutex );
  return mCharts.remove( id ) > 0;
}

Json::Value ChartRegistry::find( const QString &id ) const
{
  QMutexLocker lock( &mMutex );
  return mCharts.value( id, Json::Value() );
}

Json::Value ChartRegistry::listCharts() const
{
  QMutexLocker lock( &mMutex );
  Json::Value charts( Json::arrayValue );
  for ( auto it = mCharts.constBegin(); it != mCharts.constEnd(); ++it )
    charts.append( it.value() );
  return charts;
}

void ChartRegistry::clear()
{
  QMutexLocker lock( &mMutex );
  mCharts.clear();
}

bool renderChartToFile( const Json::Value &chart, const QString &path, QString *error )
{
  const auto problems = validateChartSpec( chart );
  if ( !problems.empty() )
  {
    if ( error )
      *error = QString::fromStdString( problems.front() );
    return false;
  }
  int width = 480;
  int height = 320;
  if ( chart.isMember( "width_px" ) && chart["width_px"].isInt() )
    width = std::clamp( chart["width_px"].asInt(), 64, 4096 );
  if ( chart.isMember( "height_px" ) && chart["height_px"].isInt() )
    height = std::clamp( chart["height_px"].asInt(), 64, 4096 );

  QImage image( width, height, QImage::Format_ARGB32_Premultiplied );
  image.fill( Qt::white );
  QPainter painter( &image );
  painter.setRenderHint( QPainter::Antialiasing, true );
  const bool ok = renderInlineChart( chart, painter, QSizeF( width, height ), error );
  painter.end();
  if ( !ok )
    return false;
  if ( !image.save( path, "PNG" ) )
  {
    if ( error )
      *error = QStringLiteral( "cannot write chart image: %1" ).arg( path );
    return false;
  }
  return true;
}

bool renderColorbarToFile( const Json::Value &colorbar, const QString &path, QString *error )
{
  // Sequential-ish default ramp; named ramps map to a small built-in set so
  // no QGIS style DB is required headless.
  static const struct
  {
    const char *name;
    QColor from;
    QColor to;
  } kRamps[] = {
    { "viridis", QColor( 0x44, 0x0a, 0x3f ), QColor( 0xfd, 0xe7, 0x25 ) },
    { "sequential", QColor( 0xf7, 0xfb, 0xf5 ), QColor( 0x00, 0x44, 0x1b ) },
    { "heat", QColor( 0xff, 0xff, 0xcc ), QColor( 0x80, 0x00, 0x26 ) },
    { "blue", QColor( 0xf7, 0xfb, 0xff ), QColor( 0x08, 0x30, 0x6b ) },
  };
  const std::string ramp =
    colorbar.isMember( "ramp" ) && colorbar["ramp"].isString() ? colorbar["ramp"].asString()
                                                               : "sequential";
  QColor from = QColor( 0xf7, 0xfb, 0xf5 );
  QColor to = QColor( 0x00, 0x44, 0x1b );
  bool matched = false;
  for ( const auto &candidate : kRamps )
    matched = matched || ( ramp == candidate.name && ( from = candidate.from, to = candidate.to, true ) );
  Q_UNUSED( matched );

  const int width = 320;
  const int height = 40;
  QImage image( width, height, QImage::Format_ARGB32_Premultiplied );
  image.fill( Qt::white );
  QPainter painter( &image );
  const QRectF bar( 8, 6, width - 16, height - 26 );
  QLinearGradient gradient( bar.topLeft(), bar.topRight() );
  gradient.setColorAt( 0.0, from );
  gradient.setColorAt( 1.0, to );
  painter.fillRect( bar, gradient );
  painter.setPen( QColor( 0x60, 0x60, 0x60 ) );
  painter.drawRect( bar );
  const std::string minLabel = colorbar.isMember( "min" ) ? colorbar["min"].asString() : "min";
  const std::string maxLabel = colorbar.isMember( "max" ) ? colorbar["max"].asString() : "max";
  painter.drawText( QRectF( 8, height - 18, width / 2 - 8, 16 ), Qt::AlignLeft,
                    QString::fromStdString( minLabel ) );
  painter.drawText( QRectF( width / 2, height - 18, width / 2 - 8, 16 ), Qt::AlignRight,
                    QString::fromStdString( maxLabel ) );
  painter.end();
  if ( !image.save( path, "PNG" ) )
  {
    if ( error )
      *error = QStringLiteral( "cannot write colorbar image: %1" ).arg( path );
    return false;
  }
  return true;
}

bool bindNativeChart( QgsLayoutItemChart *item, const Json::Value &chart, QString *error )
{
  if ( !item )
  {
    if ( error )
      *error = QStringLiteral( "null chart item" );
    return false;
  }
  const Json::Value &binding = chart["binding"];
  const QString layerRef = QString::fromStdString( binding.get( "layer", "" ).asString() );
  QgsMapLayer *layer = nullptr;
  if ( QgsProject *project = QgsProject::instance() )
  {
    const QList<QgsMapLayer *> matches = project->mapLayersByName( layerRef );
    if ( !matches.isEmpty() )
      layer = matches.first();
    else
      layer = project->mapLayer( layerRef );
  }
  QgsVectorLayer *vector = qobject_cast<QgsVectorLayer *>( layer );
  if ( !vector )
  {
    if ( error )
      *error = QStringLiteral( "chart binding layer '%1' not found or not vector" ).arg( layerRef );
    return false;
  }
  item->setSourceLayer( vector );

  const std::string kind = chart.get( "kind", "bar" ).asString();
  Qgs2DPlot *plot = nullptr;
  if ( kind == "line" || kind == "area" || kind == "scatter" )
    plot = new QgsLineChartPlot();
  else if ( kind == "pie" )
  {
    if ( error )
      *error = QStringLiteral( "pie charts use the inline rendering path" );
    return false;
  }
  else
    plot = new QgsBarChartPlot();
  item->setPlot( plot );

  QgsLayoutItemChart::SeriesDetails series(
    QString::fromStdString( binding.get( "series_name", "series" ).asString() ) );
  series.setXExpression( QString::fromStdString( binding.get( "x_expression", "" ).asString() ) );
  series.setYExpression( QString::fromStdString( binding.get( "y_expression", "" ).asString() ) );
  if ( binding.isMember( "filter" ) && binding["filter"].isString() )
    series.setFilterExpression( QString::fromStdString( binding["filter"].asString() ) );
  item->setSeriesList( { series } );
  return true;
}

} // namespace sicnu::agent::cartography
