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
#include <limits>
#include <numeric>

namespace sicnu::agent::cartography {

namespace {

const char *const kChartKinds[] = { "bar", "line", "pie", "histogram", "area", "scatter",
                                    "stacked_bar", "matrix", "metric", "grouped_bar",
                                    "table", "summary_table", "topn_table", "sparkline",
                                    "accuracy_summary" };

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

QColor styleTextColor( const Json::Value &style )
{
  if ( style.isObject() && style.isMember( "text_color" ) && style["text_color"].isString() )
  {
    const QString hex = QString::fromStdString( style["text_color"].asString() );
    if ( QColor::isValidColor( hex ) )
      return QColor( hex );
  }
  return QColor( 0x20, 0x20, 0x20 );
}

void drawAxes( QPainter &painter, const QRectF &plotRect, double maxValue, bool showGrid = true,
               const QColor &textColor = QColor( 0x20, 0x20, 0x20 ) )
{
  painter.setPen( QPen( QColor( 0x90, 0x90, 0x90 ), 1 ) );
  painter.drawLine( plotRect.bottomLeft(), plotRect.bottomRight() );
  painter.drawLine( plotRect.bottomLeft(), plotRect.topLeft() );
  if ( !showGrid )
    return;
  painter.setPen( textColor );
  for ( int tick = 0; tick <= 4; ++tick )
  {
    const double y = plotRect.bottom() - plotRect.height() * tick / 4.0;
    painter.drawLine( QPointF( plotRect.left(), y ), QPointF( plotRect.left() - 4, y ) );
    painter.drawText( QRectF( 0, y - 8, plotRect.left() - 8, 16 ), Qt::AlignRight | Qt::AlignVCenter,
                      QString::number( maxValue * tick / 4.0, 'g', 3 ) );
  }
}

/// Derives the accuracy-summary metric rows from a square confusion matrix
/// binding (rows = reference, columns = predicted): overall accuracy, kappa,
/// then per-class precision/recall in label order. Pure; empty result with
/// *error on malformed input.
std::vector<std::pair<QString, double>> deriveAccuracyRows( const Json::Value &binding,
                                                            QString *error )
{
  const Json::Value &matrix = binding.get( "matrix", Json::Value() );
  if ( !matrix.isObject() || !matrix.isMember( "labels" ) || !matrix["labels"].isArray() ||
       !matrix.isMember( "rows" ) || !matrix["rows"].isArray() || matrix["rows"].empty() ||
       matrix["rows"].size() != matrix["labels"].size() )
  {
    if ( error )
      *error = QStringLiteral(
        "accuracy_summary needs a square binding.matrix {labels: [n], rows: [n][n]}" );
    return {};
  }
  const int n = static_cast<int>( matrix["labels"].size() );
  if ( n > 64 )
  {
    if ( error )
      *error = QStringLiteral( "accuracy_summary capped at 64 classes" );
    return {};
  }
  double rowSums[64] = { 0.0 };
  double colSums[64] = { 0.0 };
  double trace = 0.0;
  double total = 0.0;
  for ( int r = 0; r < n; ++r )
  {
    const Json::Value &row = matrix["rows"][r];
    if ( !row.isArray() || static_cast<int>( row.size() ) != n )
    {
      if ( error )
        *error = QStringLiteral( "accuracy_summary matrix rows must be n×n numeric" );
      return {};
    }
    for ( int c = 0; c < n; ++c )
    {
      if ( !row[c].isNumeric() )
      {
        if ( error )
          *error = QStringLiteral( "accuracy_summary matrix cells must be numeric" );
        return {};
      }
      const double value = row[c].asDouble();
      rowSums[r] += value;
      colSums[c] += value;
      total += value;
      if ( r == c )
        trace += value;
    }
  }
  if ( total <= 0 )
  {
    if ( error )
      *error = QStringLiteral( "accuracy_summary matrix totals must be positive" );
    return {};
  }
  const double po = trace / total;
  double pe = 0.0;
  for ( int i = 0; i < n; ++i )
    pe += ( rowSums[i] * colSums[i] ) / ( total * total );
  const double kappa = pe < 1.0 ? ( po - pe ) / ( 1.0 - pe ) : 0.0;

  std::vector<std::pair<QString, double>> rows;
  rows.emplace_back( QStringLiteral( "Overall accuracy" ), po );
  rows.emplace_back( QStringLiteral( "Kappa" ), kappa );
  for ( int i = 0; i < n; ++i )
  {
    const QString label = QString::fromStdString( matrix["labels"][i].asString() );
    const double precision = colSums[i] > 0 ? matrix["rows"][i][i].asDouble() / colSums[i] : 0.0;
    const double recall = rowSums[i] > 0 ? matrix["rows"][i][i].asDouble() / rowSums[i] : 0.0;
    rows.emplace_back( QStringLiteral( "P " ) + label, precision );
    rows.emplace_back( QStringLiteral( "R " ) + label, recall );
  }
  return rows;
}

/// Shared two-value scale for the painter renderer.
bool renderInlineChart( const Json::Value &chart, QPainter &painter, const QSizeF &size,
                        QString *error )
{
  const std::string kind = chart.get( "kind", "bar" ).asString();
  const QString title = QString::fromStdString( chart.get( "title", "" ).asString() );
  const QColor textColor = styleTextColor( chart["style"] );
  bool showGrid = true;
  if ( chart.isMember( "style" ) && chart["style"].isMember( "show_grid" ) &&
       chart["style"]["show_grid"].isBool() )
    showGrid = chart["style"]["show_grid"].asBool();

  QFont font = painter.font();
  int fontPt = 10;
  if ( chart.isMember( "style" ) && chart["style"].isMember( "font_pt" ) &&
       chart["style"]["font_pt"].isNumeric() )
    fontPt = std::clamp( chart["style"]["font_pt"].asInt(), 6, 36 );
  font.setPointSizeF( fontPt );
  painter.setFont( font );

  bool dataOk = false;
  const std::vector<std::pair<QString, double>> points = inlineData( chart, &dataOk );
  // matrix/metric branches validate their own binding shapes below; the
  // generic two-value scale (points) only gates the remaining kinds.
  const bool genericKind = kind != "matrix" && kind != "metric" && kind != "accuracy_summary";
  if ( !dataOk || points.empty() )
  {
    if ( genericKind )
    {
      if ( error )
        *error = QStringLiteral( "inline chart needs binding.data with at least one entry" );
      return false;
    }
  }

  // --- matrix charts: labeled value grid (confusion/change matrices) -------
  if ( kind == "matrix" )
  {
    const Json::Value &binding = chart["binding"];
    const Json::Value &matrix = binding.get( "matrix", Json::Value() );
    if ( !matrix.isObject() || !matrix.isMember( "labels" ) || !matrix["labels"].isArray() ||
         !matrix.isMember( "rows" ) || !matrix["rows"].isArray() || matrix["rows"].empty() ||
         matrix["rows"].size() != matrix["labels"].size() )
    {
      if ( error )
        *error = QStringLiteral( "matrix chart needs binding.matrix {labels: [n], rows: [n][n]}" );
      return false;
    }
    const int n = static_cast<int>( matrix["labels"].size() );
    if ( n > 24 )
    {
      if ( error )
        *error = QStringLiteral( "matrix chart capped at 24 classes" );
      return false;
    }
    double maxCell = 0.0;
    for ( const auto &row : matrix["rows"] )
    {
      if ( !row.isArray() || static_cast<int>( row.size() ) != n )
      {
        if ( error )
          *error = QStringLiteral( "matrix rows must be n×n numeric" );
        return false;
      }
      for ( const auto &cell : row )
        if ( cell.isNumeric() )
          maxCell = std::max( maxCell, cell.asDouble() );
    }
    const bool diagonalEmphasis = chart["style"].get( "diagonal_emphasis", false ).asBool();
    const qreal labelW = painter.fontMetrics().horizontalAdvance( QStringLiteral( "8888" ) ) + 12;
    const qreal headerH = painter.fontMetrics().height() + 6;
    const qreal cellW = ( size.width() - labelW - 8 ) / n;
    const qreal cellH = ( size.height() - headerH - 26 ) / n;
    // Matrix cell fill: light→dark ramp over style.palette (token-driven).
    QColor rampFrom( 0xed, 0xf2, 0xf9 );
    QColor rampTo( 0x21, 0x71, 0xb5 );
    if ( chart.isMember( "style" ) && chart["style"].isMember( "palette" ) &&
         chart["style"]["palette"].isArray() && chart["style"]["palette"].size() >= 2 )
    {
      const QString fromHex =
        QString::fromStdString( chart["style"]["palette"][0].asString() );
      const QString toHex = QString::fromStdString(
        chart["style"]["palette"][chart["style"]["palette"].size() - 1].asString() );
      if ( QColor::isValidColor( fromHex ) && QColor::isValidColor( toHex ) )
      {
        rampFrom = QColor( fromHex );
        rampTo = QColor( toHex );
      }
    }
    painter.setPen( textColor );
    for ( int c = 0; c < n; ++c )
      painter.drawText( QRectF( labelW + c * cellW, 24, cellW, headerH ), Qt::AlignCenter,
                        QString::fromStdString( matrix["labels"][c].asString() ) );
    for ( int r = 0; r < n; ++r )
    {
      painter.drawText( QRectF( 4, 24 + headerH + r * cellH, labelW - 8, cellH ),
                        Qt::AlignVCenter | Qt::AlignRight,
                        QString::fromStdString( matrix["labels"][r].asString() ) );
      for ( int c = 0; c < n; ++c )
      {
        const Json::Value &cell = matrix["rows"][r][c];
        const double value = cell.isNumeric() ? cell.asDouble() : 0.0;
        const double t = maxCell > 0 ? value / maxCell : 0.0;
        QColor fill = rampFrom;
        if ( !( diagonalEmphasis && r == c ) )
        {
          fill.setRedF( rampFrom.redF() + ( rampTo.redF() - rampFrom.redF() ) * t );
          fill.setGreenF( rampFrom.greenF() + ( rampTo.greenF() - rampFrom.greenF() ) * t );
          fill.setBlueF( rampFrom.blueF() + ( rampTo.blueF() - rampFrom.blueF() ) * t );
        }
        else
        {
          fill = rampTo;
        }
        painter.fillRect( QRectF( labelW + c * cellW, 24 + headerH + r * cellH, cellW, cellH ),
                          fill );
        painter.setPen( t > 0.5 ? Qt::white : textColor );
        painter.drawText( QRectF( labelW + c * cellW, 24 + headerH + r * cellH, cellW, cellH ),
                          Qt::AlignCenter, QString::number( value, 'g', 3 ) );
      }
    }
    if ( !title.isEmpty() )
    {
      painter.setPen( textColor );
      painter.drawText( QRectF( 0, 4, size.width(), 20 ), Qt::AlignCenter, title );
    }
    return true;
  }

  // --- metric cards: big-value strips (OA / Kappa / F1) ---------------------
  if ( kind == "metric" )
  {
    bool metricsOk = false;
    const std::vector<std::pair<QString, double>> metrics = inlineData( chart, &metricsOk );
    if ( !metricsOk || metrics.empty() )
    {
      if ( error )
        *error = QStringLiteral( "metric chart needs binding.data with at least one entry" );
      return false;
    }
    double valueScale = 2.0;
    if ( chart.isMember( "style" ) && chart["style"].isMember( "value_font_scale" ) &&
         chart["style"]["value_font_scale"].isNumeric() )
      valueScale = std::clamp( chart["style"]["value_font_scale"].asDouble(), 1.0, 4.0 );
    const int n = static_cast<int>( metrics.size() );
    const qreal cardW = size.width() / n;
    QFont valueFont = font;
    valueFont.setPointSizeF( std::clamp( fontPt * valueScale, 8.0, 72.0 ) );
    for ( int i = 0; i < n; ++i )
    {
      const QRectF card( i * cardW + 4, 26, cardW - 8, size.height() - 46 );
      painter.setPen( QPen( paletteColor( chart["style"], i ), 2 ) );
      painter.drawRect( card );
      painter.setFont( valueFont );
      painter.setPen( textColor );
      painter.drawText( card.adjusted( 0, card.height() * 0.15, 0, 0 ), Qt::AlignHCenter,
                        QString::number( metrics[i].second, 'g', 4 ) );
      painter.setFont( font );
      painter.drawText( QRectF( card.left(), card.bottom() + 4, card.width(), 16 ),
                        Qt::AlignHCenter, metrics[i].first );
    }
    if ( !title.isEmpty() )
    {
      painter.setPen( textColor );
      painter.drawText( QRectF( 0, 4, size.width(), 20 ), Qt::AlignCenter, title );
    }
    return true;
  }

  // --- accuracy summary: confusion matrix -> deterministic metric table ----
  if ( kind == "accuracy_summary" )
  {
    QString deriveError;
    const std::vector<std::pair<QString, double>> rows =
      deriveAccuracyRows( chart["binding"], &deriveError );
    if ( rows.empty() )
    {
      if ( error )
        *error = deriveError;
      return false;
    }
    // Paint with the plain table path (kind "table": NO n/mean/sum header —
    // aggregating accuracies would be meaningless), presenting the derived
    // rows as the binding data (validation already ran on the original).
    Json::Value tableChart = chart;
    tableChart["kind"] = "table";
    tableChart["binding"]["data"] = Json::Value( Json::arrayValue );
    for ( const auto &row : rows )
    {
      Json::Value entry( Json::objectValue );
      entry["label"] = row.first.toStdString();
      entry["value"] = row.second;
      tableChart["binding"]["data"].append( entry );
    }
    return renderInlineChart( tableChart, painter, size, error );
  }

  // --- table family: label/value tables with deterministic caps (5.0) -------
  if ( kind == "table" || kind == "summary_table" || kind == "topn_table" )
  {
    if ( !dataOk || points.empty() )
    {
      if ( error )
        *error = QStringLiteral( "table charts need binding.data with at least one entry" );
      return false;
    }
    // Caps: 64 rendered rows; overflows keep a deterministic trailing
    // "+N more" row so nothing disappears silently. topn sorts descending
    // before the cap.
    constexpr int kMaxTableRows = 64;
    std::vector<std::pair<QString, double>> rows = points;
    rows.erase( std::remove_if( rows.begin(), rows.end(),
                                []( const auto &row ) { return !std::isfinite( row.second ); } ),
                rows.end() );
    if ( rows.empty() )
    {
      if ( error )
        *error = QStringLiteral( "table charts need at least one finite value" );
      return false;
    }
    int topN = 10;
    if ( kind == "topn_table" )
    {
      if ( chart.isMember( "style" ) && chart["style"].isMember( "top_n" ) &&
           chart["style"]["top_n"].isIntegral() )
        topN = std::clamp( chart["style"]["top_n"].asInt(), 1, kMaxTableRows );
      std::stable_sort( rows.begin(), rows.end(),
                        []( const auto &a, const auto &b ) { return a.second > b.second; } );
      if ( static_cast<int>( rows.size() ) > topN )
        rows.resize( topN );
    }
    // Every kind honors the 64-row render budget; the overflow row reports
    // what was left out (review fix: plain tables silently dropped rows).
    if ( static_cast<int>( rows.size() ) > kMaxTableRows )
      rows.resize( kMaxTableRows );
    const int hidden =
      static_cast<int>( points.size() ) - static_cast<int>( rows.size() );
    const bool withSummary = kind == "summary_table";
    const QString labelHeader =
      chart.isMember( "style" ) && chart["style"].isMember( "label_column" ) &&
          chart["style"]["label_column"].isString()
        ? QString::fromStdString( chart["style"]["label_column"].asString() )
        : QStringLiteral( "Class" );
    const QString valueHeader =
      chart.isMember( "style" ) && chart["style"].isMember( "value_column" ) &&
          chart["style"]["value_column"].isString()
        ? QString::fromStdString( chart["style"]["value_column"].asString() )
        : QStringLiteral( "Value" );

    qreal y = 24;
    const qreal rowH = painter.fontMetrics().height() + 4;
    const qreal labelW = ( size.width() - 8 ) * 0.62;
    const qreal valueW = ( size.width() - 8 ) * 0.38;
    auto drawRow = [ & ]( const QString &label, const QString &value, bool header,
                          bool zebra ) {
      if ( header )
      {
        painter.setPen( textColor );
        painter.drawText( QRectF( 4, y, labelW, rowH ), Qt::AlignLeft | Qt::AlignVCenter, label );
        painter.drawText( QRectF( 4 + labelW, y, valueW, rowH ),
                          Qt::AlignRight | Qt::AlignVCenter, value );
        y += rowH + 1;
        painter.setPen( QPen( QColor( 0x90, 0x90, 0x90 ), 1 ) );
        painter.drawLine( QPointF( 4, y ), QPointF( size.width() - 4, y ) );
        return;
      }
      if ( zebra )
        painter.fillRect( QRectF( 4, y, size.width() - 8, rowH ), QColor( 0xf2, 0xf4, 0xf7 ) );
      painter.setPen( textColor );
      // Long/CJK labels elide deterministically at the column width.
      painter.drawText( QRectF( 4, y, labelW, rowH ), Qt::AlignLeft | Qt::AlignVCenter,
                        painter.fontMetrics().elidedText( label, Qt::ElideRight,
                                                          static_cast<int>( labelW - 8 ) ) );
      painter.drawText( QRectF( 4 + labelW, y, valueW, rowH ),
                        Qt::AlignRight | Qt::AlignVCenter, value );
      y += rowH;
    };

    drawRow( labelHeader, valueHeader, true, false );
    if ( withSummary )
    {
      double sum = 0.0;
      double minV = rows.front().second;
      double maxV = rows.front().second;
      for ( const auto &row : rows )
      {
        sum += row.second;
        minV = std::min( minV, row.second );
        maxV = std::max( maxV, row.second );
      }
      const double mean = sum / rows.size();
      drawRow( QStringLiteral( "n / mean" ),
               QStringLiteral( "%1 / %2" ).arg( rows.size() ).arg( mean, 0, 'g', 4 ), false, true );
      drawRow( QStringLiteral( "sum / min–max" ),
               QStringLiteral( "%1 / %2–%3" ).arg( sum, 0, 'g', 4 ).arg( minV, 0, 'g', 4 ).arg( maxV, 0, 'g', 4 ),
               false, false );
    }
    int rendered = 0;
    for ( const auto &row : rows )
    {
      if ( rendered >= kMaxTableRows )
        break;
      drawRow( QString::number( rendered + 1 ) + QLatin1String( ". " ) + row.first,
               QString::number( row.second, 'g', 6 ), false, rendered % 2 == 0 );
      ++rendered;
    }
    if ( hidden > 0 )
      drawRow( QStringLiteral( "… + %1 more" ).arg( hidden ), QString(), false, false );
    if ( !title.isEmpty() )
    {
      painter.setPen( textColor );
      painter.drawText( QRectF( 0, 4, size.width(), 20 ), Qt::AlignCenter, title );
    }
    return true;
  }

  // --- sparkline: axes-free compact trend with baseline ---------------------
  if ( kind == "sparkline" )
  {
    if ( !dataOk || points.size() < 2 )
    {
      if ( error )
        *error = QStringLiteral( "sparkline needs binding.data with at least two entries" );
      return false;
    }
    double minV = std::numeric_limits<double>::infinity();
    double maxV = -std::numeric_limits<double>::infinity();
    for ( const auto &point : points )
    {
      if ( !std::isfinite( point.second ) )
        continue;
      minV = std::min( minV, point.second );
      maxV = std::max( maxV, point.second );
    }
    if ( !std::isfinite( minV ) )
    {
      if ( error )
        *error = QStringLiteral( "sparkline needs at least one finite value" );
      return false;
    }
    if ( maxV - minV < 1e-12 )
      maxV = minV + 1.0;
    const QRectF area( 6.0, 26.0, size.width() - 12.0, size.height() - 38.0 );
    painter.setRenderHint( QPainter::Antialiasing, true );
    QPainterPath path;
    bool started = false;
    for ( int i = 0; i < static_cast<int>( points.size() ); ++i )
    {
      if ( !std::isfinite( points[i].second ) )
        continue;
      const double x = area.left() + area.width() * i / std::max<qreal>( 1, points.size() - 1 );
      const double t = ( points[i].second - minV ) / ( maxV - minV );
      const double y = area.bottom() - area.height() * t;
      if ( !started )
      {
        path.moveTo( x, y );
        started = true;
      }
      else
      {
        path.lineTo( x, y );
      }
    }
    painter.setPen( QPen( paletteColor( chart["style"], 0 ), 2 ) );
    painter.drawPath( path );
    painter.setPen( QPen( QColor( 0xbb, 0xbb, 0xbb ), 1, Qt::DashLine ) );
    painter.drawLine( QPointF( area.left(), area.bottom() ), QPointF( area.right(), area.bottom() ) );
    if ( !title.isEmpty() )
    {
      painter.setPen( textColor );
      painter.drawText( QRectF( 0, 4, size.width(), 20 ), Qt::AlignCenter, title );
    }
    return true;
  }

  // --- grouped bars: real side-by-side segments per label (5.0) -------------
  if ( kind == "grouped_bar" )
  {
    const Json::Value &data = chart.isMember( "binding" ) && chart["binding"].isMember( "data" )
                                ? chart["binding"]["data"]
                                : Json::Value::nullSingleton();
    if ( !data.isArray() || data.empty() )
    {
      if ( error )
        *error = QStringLiteral( "grouped_bar needs binding.data entries" );
      return false;
    }
    // Series legend = distinct part labels in first-seen order.
    QStringList seriesLabels;
    double maxPart = 0.0;
    for ( const auto &entry : data )
    {
      if ( !entry.isObject() || !entry.isMember( "parts" ) || !entry["parts"].isArray() )
      {
        if ( error )
          *error = QStringLiteral( "grouped_bar entries need parts arrays" );
        return false;
      }
      for ( const auto &part : entry["parts"] )
      {
        if ( !part.isObject() )
          continue;
        const double value = part.isMember( "value" ) && part["value"].isNumeric()
                               ? part["value"].asDouble()
                               : 0.0;
        if ( std::isfinite( value ) )
          maxPart = std::max( maxPart, std::fabs( value ) );
        const QString label = QString::fromStdString( part.get( "label", "" ).asString() );
        if ( !label.isEmpty() && !seriesLabels.contains( label ) )
          seriesLabels << label;
      }
    }
    if ( maxPart <= 0 )
      maxPart = 1.0;

    QRectF plotRect( 44.0, 44.0, size.width() - 54.0, size.height() - 66.0 );
    drawAxes( painter, plotRect, maxPart, showGrid, textColor );
    const int groups = static_cast<int>( data.size() );
    const double groupWidth = plotRect.width() / std::max( 1, groups );
    const int series = static_cast<int>( std::max<qsizetype>( 1, seriesLabels.size() ) );
    const double barWidth = std::max( 2.0, groupWidth / series - 2.0 );
    for ( int g = 0; g < groups; ++g )
    {
      const Json::Value &entry = data[g];
      int s = 0;
      for ( const auto &part : entry.get( "parts", Json::Value() ) )
      {
        if ( !part.isObject() )
          continue;
        const double value = part.isMember( "value" ) && part["value"].isNumeric()
                               ? part["value"].asDouble()
                               : 0.0;
        const double h = plotRect.height() * std::fabs( value ) / maxPart;
        const double x = plotRect.left() + g * groupWidth + s * ( barWidth + 2.0 );
        painter.setBrush( paletteColor( chart["style"], s ) );
        painter.setPen( Qt::NoPen );
        painter.drawRect( QRectF( x, plotRect.bottom() - h, barWidth, h ) );
        ++s;
      }
      const QString label = QString::fromStdString( entry.get( "label", "" ).asString() );
      if ( !label.isEmpty() && groupWidth > 12 )
      {
        painter.setPen( textColor );
        painter.drawText( QRectF( plotRect.left() + g * groupWidth, plotRect.bottom() + 4, groupWidth, 18 ),
                          Qt::AlignCenter, label );
      }
    }
    // Compact series legend across the top.
    if ( !seriesLabels.isEmpty() )
    {
      qreal legendX = 44.0;
      for ( int s = 0; s < seriesLabels.size(); ++s )
      {
        painter.fillRect( QRectF( legendX, 30, 8, 8 ), paletteColor( chart["style"], s ) );
        painter.setPen( textColor );
        painter.drawText( QRectF( legendX + 11, 24, 90, 18 ), Qt::AlignLeft | Qt::AlignVCenter,
                          painter.fontMetrics().elidedText( seriesLabels[s], Qt::ElideRight, 90 ) );
        legendX += 108;
        if ( legendX > size.width() - 60 )
          break;
      }
    }
    if ( !title.isEmpty() )
    {
      painter.setPen( textColor );
      painter.drawText( QRectF( 0, 4, size.width(), 20 ), Qt::AlignCenter, title );
    }
    return true;
  }

  if ( points.empty() ) // re-check for the generic kinds (matrix/metric returned above)
  {
    if ( error )
      *error = QStringLiteral( "inline chart needs binding.data with at least one entry" );
    return false;
  }

  double maxValue = 0.0;
  for ( int i = 0; i < static_cast<int>( points.size() ); ++i )
  {
    double magnitude = std::fabs( points[i].second );
    // Stacked entries scale by the sum of their parts.
    if ( kind == "stacked_bar" && chart.isMember( "binding" ) &&
         chart["binding"].isMember( "data" ) && chart["binding"]["data"].isArray() &&
         i < static_cast<int>( chart["binding"]["data"].size() ) )
    {
      const Json::Value &entry = chart["binding"]["data"][i];
      if ( entry.isObject() && entry.isMember( "parts" ) && entry["parts"].isArray() )
      {
        double sum = 0.0;
        for ( const auto &part : entry["parts"] )
          if ( part.isObject() && part.isMember( "value" ) && part["value"].isNumeric() )
            sum += std::fabs( part["value"].asDouble() );
        magnitude = sum;
      }
    }
    maxValue = std::max( maxValue, magnitude );
  }
  if ( maxValue <= 0 )
    maxValue = 1.0;

  // A justified secondary axis reserves a right margin for its tick labels
  // (validation guarantees justification + line/scatter + array shape).
  const bool hasSecondaryAxis =
    ( kind == "line" || kind == "scatter" ) && chart.isMember( "axes" ) &&
    chart["axes"].isObject() && chart["axes"].isMember( "secondary" ) &&
    chart["axes"]["secondary"].isObject();
  const double rightMargin = hasSecondaryAxis ? 104.0 : 10.0;
  QRectF plotRect( 44.0, 30.0, size.width() - 44.0 - rightMargin, size.height() - 56.0 );

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
  else if ( kind == "bar" || kind == "histogram" || kind == "stacked_bar" )
  {
    drawAxes( painter, plotRect, maxValue, showGrid, textColor );
    const int n = static_cast<int>( points.size() );
    const double barWidth = plotRect.width() / std::max( 1, n );
    for ( int i = 0; i < n; ++i )
    {
      if ( kind == "stacked_bar" )
      {
        // Entries may carry {label, parts: [{label, value}]}; plain
        // {label, value} renders as a single segment.
        const Json::Value &entry =
          chart["binding"].isMember( "data" ) && chart["binding"]["data"].isArray() &&
              i < static_cast<Json::Value::ArrayIndex>( chart["binding"]["data"].size() ) &&
              chart["binding"]["data"][i].isObject()
            ? chart["binding"]["data"][i]
            : Json::Value::nullSingleton();
        const Json::Value &parts =
          entry.isObject() && entry.isMember( "parts" ) && entry["parts"].isArray()
            ? entry["parts"]
            : Json::Value();
        double bottom = plotRect.bottom();
        if ( parts.isArray() && !parts.empty() )
        {
          int segment = 0;
          for ( const auto &part : parts )
          {
            const double value =
              part.isObject() && part.isMember( "value" ) && part["value"].isNumeric()
                ? part["value"].asDouble()
                : 0.0;
            const double h = plotRect.height() * std::fabs( value ) / maxValue;
            painter.setBrush( paletteColor( chart["style"], segment ) );
            painter.setPen( Qt::NoPen );
            painter.drawRect(
              QRectF( plotRect.left() + i * barWidth + 1, bottom - h,
                      std::max( 2.0, barWidth - 2.0 ), h ) );
            bottom -= h;
            ++segment;
          }
        }
        else
        {
          const double h = plotRect.height() * std::fabs( points[i].second ) / maxValue;
          painter.setBrush( paletteColor( chart["style"], i ) );
          painter.setPen( Qt::NoPen );
          painter.drawRect( QRectF( plotRect.left() + i * barWidth + 1, bottom - h,
                                    std::max( 2.0, barWidth - 2.0 ), h ) );
        }
      }
      else
      {
        const double h = plotRect.height() * std::fabs( points[i].second ) / maxValue;
        const QRectF bar( plotRect.left() + i * barWidth + 1, plotRect.bottom() - h,
                          std::max( 2.0, barWidth - 2.0 ), h );
        painter.setBrush( paletteColor( chart["style"], i ) );
        painter.setPen( Qt::NoPen );
        painter.drawRect( bar );
      }
      if ( !points[i].first.isEmpty() && barWidth > 12 )
      {
        painter.setPen( textColor );
        painter.drawText( QRectF( plotRect.left() + i * barWidth, plotRect.bottom() + 4, barWidth, 18 ),
                          Qt::AlignCenter, points[i].first );
      }
    }
  }
  else // line / area / scatter
  {
    drawAxes( painter, plotRect, maxValue, showGrid, textColor );
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

  // Platform 7.0 dual-axis overlay (validation guarantees a justification
  // exists and the kind is line/scatter): the secondary series maps to the
  // right edge with its own scale and right-side tick labels.
  if ( kind == "line" || kind == "scatter" )
  {
    const Json::Value &axes = chart.get( "axes", Json::Value() );
    if ( axes.isObject() && axes.isMember( "secondary" ) && axes["secondary"].isObject() )
    {
      const Json::Value &secondary = axes["secondary"];
      const Json::Value &data = secondary.get( "data", Json::Value() );
      if ( data.isArray() && !data.empty() && data.size() != points.size() )
      {
        // Validation rejects length mismatches; this defensive branch keeps
        // the renderer honest if it is ever called unvalidated.
        if ( error )
          *error = QStringLiteral( "axes.secondary.data must have one value per series point" );
        return false;
      }
      if ( data.isArray() && !data.empty() )
      {
        double secondaryMax = 0.0;
        std::vector<double> values;
        for ( const auto &entry : data )
        {
          const double value =
            entry.isObject() && entry.isMember( "value" ) && entry["value"].isNumeric()
              ? entry["value"].asDouble()
              : 0.0;
          values.push_back( value );
          secondaryMax = std::max( secondaryMax, std::fabs( value ) );
        }
        if ( secondaryMax <= 0 )
          secondaryMax = 1.0;
        // Right-side axis: 5 ticks, labels in the right margin.
        painter.setPen( textColor );
        for ( int t = 0; t <= 4; ++t )
        {
          const double y = plotRect.bottom() - plotRect.height() * t / 4.0;
          painter.drawLine( QPointF( plotRect.right(), y ), QPointF( plotRect.right() + 4, y ) );
          painter.drawText( QRectF( plotRect.right() + 6, y - 8, 44, 16 ),
                            Qt::AlignLeft | Qt::AlignVCenter,
                            QString::number( secondaryMax * t / 4.0, 'g', 3 ) );
        }
        // Secondary series as a dashed line over the primary x positions.
        const int n = static_cast<int>( points.size() );
        const double stepX = n > 1 ? plotRect.width() / ( n - 1 ) : 0.0;
        QPen secondaryPen( paletteColor( chart["style"], 1 ), 2, Qt::DashLine );
        painter.setPen( secondaryPen );
        QPainterPath secondaryPath;
        for ( int i = 0; i < n; ++i )
        {
          const double x = plotRect.left() + i * stepX;
          const double y =
            plotRect.bottom() - plotRect.height() * std::fabs( values[i] ) / secondaryMax;
          if ( i == 0 )
            secondaryPath.moveTo( x, y );
          else
            secondaryPath.lineTo( x, y );
        }
        painter.drawPath( secondaryPath );
      }
    }
  }

  if ( !title.isEmpty() )
  {
    painter.setPen( textColor );
    painter.drawText( QRectF( 0, 4, size.width(), 24 ), Qt::AlignCenter, title );
  }
  return true;
}

} // namespace

std::vector<std::pair<QString, double>> deriveAccuracySummaryRows( const Json::Value &binding,
                                                                   QString *error )
{
  return deriveAccuracyRows( binding, error );
}

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
    problems.push_back(
      "kind must be one of bar|line|pie|histogram|area|scatter|stacked_bar|matrix|metric|"
      "grouped_bar|table|summary_table|topn_table|sparkline|accuracy_summary" );
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
  if ( mode == "inline" && ( kind == "matrix" || kind == "accuracy_summary" ) )
  {
    const Json::Value &matrix = binding.get( "matrix", Json::Value() );
    if ( !matrix.isObject() || !matrix.isMember( "labels" ) || !matrix["labels"].isArray() ||
         !matrix.isMember( "rows" ) || !matrix["rows"].isArray() )
      problems.push_back( kind == "accuracy_summary"
                            ? "accuracy_summary charts need binding.matrix {labels, rows} "
                              "(square confusion matrix)"
                            : "matrix charts need binding.matrix {labels, rows}" );
    else if ( kind == "accuracy_summary" )
    {
      if ( matrix["rows"].empty() || matrix["rows"].size() != matrix["labels"].size() )
        problems.push_back( "accuracy_summary needs a square confusion matrix "
                            "(rows = reference, columns = predicted)" );
      if ( static_cast<int>( matrix["labels"].size() ) > 31 )
        problems.push_back( "accuracy_summary capped at 31 classes so the metric "
                            "table fits its 64-row budget" );
      int labelIndex = 0;
      for ( const auto &label : matrix["labels"] )
      {
        if ( !label.isString() )
          problems.push_back( "accuracy_summary labels[" + std::to_string( labelIndex ) +
                              "] must be a string" );
        ++labelIndex;
      }
      int rowIndex = 0;
      for ( const auto &row : matrix["rows"] )
      {
        int cellIndex = 0;
        if ( row.isArray() )
          for ( const auto &cell : row )
          {
            if ( !cell.isNumeric() )
              problems.push_back( "accuracy_summary matrix rows must be numeric "
                                  "(row " + std::to_string( rowIndex ) + ", cell " +
                                  std::to_string( cellIndex ) + ")" );
            ++cellIndex;
          }
        ++rowIndex;
      }
    }
  }
  if ( kind == "accuracy_summary" && mode != "inline" )
    problems.push_back( "accuracy_summary derives from inline data only "
                        "(no vector_expression binding)" );

  // Platform 7.0 dual-axis policy: a secondary axis is accepted ONLY with an
  // explicit semantic justification — otherwise validation rejects it (no
  // silent single-axis downgrade, no unjustified dual axes).
  if ( chart.isMember( "axes" ) )
  {
    const Json::Value &axes = chart["axes"];
    if ( !axes.isObject() )
      problems.push_back( "axes must be an object" );
    else if ( axes.isMember( "secondary" ) )
    {
      const Json::Value &secondary = axes["secondary"];
      if ( !secondary.isObject() )
        problems.push_back( "axes.secondary must be an object" );
      else
      {
        const bool justified = secondary.isMember( "justification" ) &&
                               secondary["justification"].isString() &&
                               !secondary["justification"].asString().empty();
        if ( !justified )
          problems.push_back( "dual axis requires axes.secondary.justification stating the "
                              "semantic reason (different units/scale); without it the chart "
                              "is rejected" );
        if ( kind != "line" && kind != "scatter" )
          problems.push_back( "dual axis is only defined for line/scatter charts" );
        if ( secondary.isMember( "data" ) && !secondary["data"].isArray() )
        {
          problems.push_back( "axes.secondary.data must be an array of {label, value}" );
        }
        else if ( secondary.isMember( "data" ) )
        {
          const Json::Value &secondaryData = secondary["data"];
          const Json::Value &primaryData = binding.get( "data", Json::Value() );
          if ( primaryData.isArray() && secondaryData.size() != primaryData.size() )
            problems.push_back( "axes.secondary.data must have one value per series point" );
        }
      }
    }
  }
  // Standalone check (the 7.0 axes block above broke the historical
  // else-if chain): matrix-family kinds validate their own binding shape.
  if ( mode == "inline" && kind != "matrix" && kind != "accuracy_summary" )
  {
    if ( !binding.isMember( "data" ) || !binding["data"].isArray() || binding["data"].empty() )
      problems.push_back( "inline binding needs non-empty data array" );
    else if ( binding["data"].size() > 256 )
      problems.push_back( "inline binding capped at 256 data points" );
  }
  if ( mode == "vector_expression" )
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
  // Two sources of ramp geometry: an explicit `colors` stop array (hex, 2+,
  // evenly spread across the bar — the token-driven path) or a named built-in
  // two-color ramp so headless runs need no QGIS style DB.
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

  QList<QColor> stops;
  if ( colorbar.isMember( "colors" ) && colorbar["colors"].isArray() &&
       colorbar["colors"].size() >= 2 )
  {
    for ( const auto &stop : colorbar["colors"] )
    {
      if ( !stop.isString() )
        continue;
      const QString hex = QString::fromStdString( stop.asString() );
      if ( QColor::isValidColor( hex ) )
        stops << QColor( hex );
    }
  }
  if ( stops.size() < 2 )
  {
    stops.clear();
    stops << from << to;
  }

  QColor textColor( 0x60, 0x60, 0x60 );
  if ( colorbar.isMember( "text_color" ) && colorbar["text_color"].isString() )
  {
    const QString hex = QString::fromStdString( colorbar["text_color"].asString() );
    if ( QColor::isValidColor( hex ) )
      textColor = QColor( hex );
  }
  double fontPt = 8.0;
  if ( colorbar.isMember( "font_pt" ) && colorbar["font_pt"].isNumeric() )
    fontPt = std::clamp( colorbar["font_pt"].asDouble(), 4.0, 24.0 );

  const int width = 320;
  const int height = 40;
  QImage image( width, height, QImage::Format_ARGB32_Premultiplied );
  image.fill( Qt::white );
  QPainter painter( &image );
  const QRectF bar( 8, 6, width - 16, height - 26 );
  const double span = 1.0 / static_cast<double>( stops.size() - 1 );
  QLinearGradient gradient( bar.topLeft(), bar.topRight() );
  for ( int i = 0; i < stops.size(); ++i )
    gradient.setColorAt( std::clamp( i * span, 0.0, 1.0 ), stops[i] );
  painter.fillRect( bar, gradient );
  painter.setPen( QColor( 0x60, 0x60, 0x60 ) );
  painter.drawRect( bar );
  QFont font = painter.font();
  font.setPointSizeF( fontPt );
  painter.setFont( font );
  painter.setPen( textColor );
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
