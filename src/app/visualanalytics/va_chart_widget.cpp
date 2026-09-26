/***************************************************************************
 * va_chart_widget.cpp — QPainter chart host over VA payloads
 ***************************************************************************/
#include "va_chart_widget.h"

#include "design_tokens.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace sicnu::app::va
{

namespace
{
namespace Tokens = SicnuUi::Tokens;

/// Draw caps: a hostile payload must never stall the UI thread. Sources
/// bound payloads upstream; these caps are the painter's own seatbelt.
constexpr int kMaxDrawnPoints = 4096;
constexpr int kMaxDrawnBars = 256;
constexpr int kMaxDrawnMatrixCells = 4096;

/// Categorical palette drawn ENTIRELY from theme tokens (rule: no local
/// QColor literals for theme roles). Both themes re-order for contrast.
QVector<QColor> categoricalPalette( bool dark )
{
    // Eight categorical slots per theme, every slot an existing token (the
    // Dark namespace declares no domain colors — accentHover/ok/inkSecondary
    // stand in there).
    if ( dark )
        return { Tokens::Dark::accent, Tokens::Dark::mapSelect, Tokens::Dark::warn,
                 Tokens::Dark::err, Tokens::Dark::ai, Tokens::Dark::accentHover,
                 Tokens::Dark::ok, Tokens::Dark::inkSecondary };
    return { Tokens::Light::accent, Tokens::Light::mapSelect, Tokens::Light::warn,
             Tokens::Light::err, Tokens::Light::ai, Tokens::Light::veg,
             Tokens::Light::water, Tokens::Light::soil };
}

QString formatNumber( double v )
{
    if ( std::abs( v ) >= 1e6 || ( std::abs( v ) > 0 && std::abs( v ) < 1e-3 ) )
        return QString::number( v, 'g', 3 );
    return QString::number( v, 'f', 3 );
}

/// Nice tick steps (1/2/5×10^k) bounded to ≤ ~6 ticks per axis.
QVector<double> niceTicks( double min, double max, int maxTicks )
{
    QVector<double> ticks;
    if ( !( max > min ) )
        return ticks;
    const double span = max - min;
    double step = std::pow( 10.0, std::floor( std::log10( span / maxTicks ) ) );
    for ( const double m : { 10.0, 5.0, 2.0, 1.0 } )
    {
        if ( span / ( step * m ) <= maxTicks )
        {
            step *= m;
            break;
        }
    }
    const double first = std::ceil( min / step ) * step;
    for ( double t = first; t <= max + step * 1e-9; t += step )
        ticks.append( t );
    return ticks;
}

} // namespace

VaChartWidget::VaChartWidget( QWidget *parent )
    : QWidget( parent )
{
    setObjectName( QStringLiteral( "rsVaChart" ) );
    setFocusPolicy( Qt::StrongFocus );
    setMouseTracking( true );
    setMinimumSize( minimumSizeHint() );
    setAccessibleName( tr( "Visualization chart" ) );
}

void VaChartWidget::setData( const VaData &data )
{
    m_data = data;
    m_mode = Mode::Ready;
    m_hasRange = false;
    m_highlightIndex = -1;
    setAccessibleDescription( QStringLiteral( "%1" ).arg( static_cast<int>( data.kind ) ) );
    update();
}

void VaChartWidget::setLoading( const QString &message )
{
    m_mode = Mode::Loading;
    m_message = message.isEmpty() ? tr( "Loading…" ) : message;
    m_hasRange = false;
    update();
}

void VaChartWidget::setError( const QString &message )
{
    m_mode = Mode::Error;
    m_message = message;
    m_hasRange = false;
    update();
}

void VaChartWidget::clear()
{
    m_data = VaData();
    m_mode = Mode::Empty;
    m_message.clear();
    m_hasRange = false;
    update();
}

QRectF VaChartWidget::plotRect() const
{
    const qreal left = 46;
    const qreal top = 10;
    const qreal right = width() - 14;
    const qreal bottom = height() - 30;
    if ( right <= left || bottom <= top )
        return QRectF();
    return QRectF( left, top, right - left, bottom - top );
}

void VaChartWidget::computeDataBounds( double *x0, double *x1, double *y0, double *y1 ) const
{
    // Families decide their own default domains; every branch returns a
    // non-degenerate box (a degenerate box would divide by zero later).
    switch ( m_data.kind )
    {
        case VaChartKind::Histogram:
        {
            *x0 = m_data.histogram.binEdges.isEmpty() ? 0 : m_data.histogram.binEdges.first();
            *x1 = m_data.histogram.binEdges.isEmpty() ? 1 : m_data.histogram.binEdges.last();
            qint64 maxY = 1;
            for ( qint64 c : m_data.histogram.counts )
                maxY = std::max( maxY, c );
            *y0 = 0;
            *y1 = static_cast<double>( maxY );
            break;
        }
        case VaChartKind::Series:
        {
            *x0 = m_data.series.xs.isEmpty() ? 0 : *std::min_element( m_data.series.xs.cbegin(), m_data.series.xs.cend() );
            *x1 = m_data.series.xs.isEmpty() ? 1 : *std::max_element( m_data.series.xs.cbegin(), m_data.series.xs.cend() );
            *y0 = m_data.series.ys.isEmpty() ? 0 : *std::min_element( m_data.series.ys.cbegin(), m_data.series.ys.cend() );
            *y1 = m_data.series.ys.isEmpty() ? 1 : *std::max_element( m_data.series.ys.cbegin(), m_data.series.ys.cend() );
            if ( *x1 <= *x0 ) *x1 = *x0 + 1;
            if ( *y1 <= *y0 ) { *y1 += 0.5; *y0 -= 0.5; }
            break;
        }
        case VaChartKind::Scatter:
        {
            const int n = std::min( m_data.scatter.xs.size(), m_data.scatter.ys.size() );
            *x0 = *y0 = std::numeric_limits<double>::infinity();
            *x1 = *y1 = -std::numeric_limits<double>::infinity();
            for ( int i = 0; i < n; ++i )
            {
                *x0 = std::min( *x0, m_data.scatter.xs.at( i ) );
                *x1 = std::max( *x1, m_data.scatter.xs.at( i ) );
                *y0 = std::min( *y0, m_data.scatter.ys.at( i ) );
                *y1 = std::max( *y1, m_data.scatter.ys.at( i ) );
            }
            if ( !std::isfinite( *x0 ) ) { *x0 = 0; *x1 = 1; *y0 = 0; *y1 = 1; }
            if ( *x1 <= *x0 ) *x1 = *x0 + 1;
            if ( *y1 <= *y0 ) { *y1 += 0.5; *y0 -= 0.5; }
            break;
        }
        case VaChartKind::BoxPlot:
        {
            *x0 = 0;
            *x1 = std::max<qreal>( 1, m_data.boxPlot.boxes.size() );
            *y0 = std::numeric_limits<double>::infinity();
            *y1 = -std::numeric_limits<double>::infinity();
            for ( const VaBox &box : m_data.boxPlot.boxes )
            {
                *y0 = std::min( *y0, box.q0 );
                *y1 = std::max( *y1, box.q4 );
                for ( double o : box.outliers )
                {
                    *y0 = std::min( *y0, o );
                    *y1 = std::max( *y1, o );
                }
            }
            if ( !std::isfinite( *y0 ) ) { *y0 = 0; *y1 = 1; }
            if ( *y1 <= *y0 ) { *y1 += 0.5; *y0 -= 0.5; }
            break;
        }
        case VaChartKind::Matrix:
        case VaChartKind::Areas:
        default:
        {
            *x0 = 0;
            *x1 = 1;
            *y0 = 0;
            *y1 = 1;
            break;
        }
    }
}

QPointF VaChartWidget::toPixel( double x, double y, const QRectF &plot,
                                const double bounds[4] ) const
{
    const double fx = ( x - bounds[0] ) / std::max( 1e-12, bounds[1] - bounds[0] );
    const double fy = ( y - bounds[2] ) / std::max( 1e-12, bounds[3] - bounds[2] );
    return { plot.left() + fx * plot.width(), plot.bottom() - fy * plot.height() };
}

bool VaChartWidget::toData( const QPoint &pixel, double *x, double *y ) const
{
    const QRectF plot = plotRect();
    if ( plot.isNull() || !plot.contains( pixel ) )
        return false;
    double b0, b1, b2, b3;
    computeDataBounds( &b0, &b1, &b2, &b3 );
    *x = b0 + ( pixel.x() - plot.left() ) / plot.width() * ( b1 - b0 );
    *y = b2 + ( plot.bottom() - pixel.y() ) / plot.height() * ( b3 - b2 );
    return true;
}

void VaChartWidget::paintEvent( QPaintEvent *event )
{
    Q_UNUSED( event );
    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing, true );

    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    painter.fillRect( rect(), dark ? Tokens::Dark::panel : Tokens::Light::panel );

    switch ( m_mode )
    {
        case Mode::Empty:
            paintState( painter, tr( "No data yet" ) );
            return;
        case Mode::Loading:
            paintState( painter, m_message );
            return;
        case Mode::Error:
            paintState( painter, tr( "Load failed: %1" ).arg( m_message ) );
            return;
        case Mode::Ready:
            break;
    }

    const QRectF plot = plotRect();
    if ( plot.isNull() )
        return;

    double b0, b1, b2, b3;
    computeDataBounds( &b0, &b1, &b2, &b3 );
    const double bounds[4] = { b0, b1, b2, b3 };
    const QColor accent = dark ? Tokens::Dark::accent : Tokens::Light::accent;

    painter.setClipRect( plot, Qt::IntersectClip );
    switch ( m_data.kind )
    {
        case VaChartKind::Histogram: paintHistogram( painter, plot ); break;
        case VaChartKind::Series: paintSeries( painter, plot ); break;
        case VaChartKind::Scatter: paintScatter( painter, plot ); break;
        case VaChartKind::BoxPlot: paintBoxPlot( painter, plot ); break;
        case VaChartKind::Matrix: paintMatrix( painter, plot ); break;
        case VaChartKind::Areas: paintAreas( painter, plot ); break;
    }
    painter.setClipping( false );

    paintAxes( painter, plot );
    paintNiceTicks( painter, plot, bounds );

    // Brushed range overlay (histogram/series).
    if ( m_hasRange )
    {
        const QPointF p0 = toPixel( m_dragStart, bounds[2], plot, bounds );
        const QPointF p1 = toPixel( m_dragCurrent, bounds[3], plot, bounds );
        QRectF band( std::min( p0.x(), p1.x() ), plot.top(),
                     std::abs( p1.x() - p0.x() ), plot.height() );
        QColor sel = dark ? Tokens::Dark::mapSelect : Tokens::Light::mapSelect;
        sel.setAlpha( 46 );
        painter.fillRect( band, sel );
        painter.setPen( sel );
        painter.drawRect( band );
    }
}

void VaChartWidget::paintState( QPainter &painter, const QString &text )
{
    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    painter.setPen( dark ? Tokens::Dark::inkSecondary : Tokens::Light::inkSecondary );
    painter.drawText( rect(), Qt::AlignCenter, text );
}

void VaChartWidget::paintHistogram( QPainter &painter, const QRectF &plot )
{
    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    double b0, b1, b2, b3;
    computeDataBounds( &b0, &b1, &b2, &b3 );
    const double bounds[4] = { b0, b1, b2, b3 };
    const QColor accent = dark ? Tokens::Dark::accent : Tokens::Light::accent;

    const int bins = static_cast<int>( std::min<qsizetype>( m_data.histogram.counts.size(),
                                                            kMaxDrawnBars ) );
    if ( bins == 0 || m_data.histogram.binEdges.size() < 2 )
        return;
    const double binWidth =
        ( m_data.histogram.binEdges.at( bins ) - m_data.histogram.binEdges.at( 0 ) ) / bins;

    painter.setBrush( accent );
    painter.setPen( Qt::NoPen );
    for ( int i = 0; i < bins; ++i )
    {
        const double x0 = m_data.histogram.binEdges.at( i );
        const QPointF p = toPixel( x0, 0, plot, bounds );
        const QPointF top = toPixel( x0, m_data.histogram.counts.at( i ), plot, bounds );
        const double w = std::max<qreal>( 1.0, binWidth / ( b1 - b0 ) * plot.width() - 1.0 );
        painter.drawRect( QRectF( p.x(), top.y(), w, plot.bottom() - top.y() ) );
    }
}

void VaChartWidget::paintSeries( QPainter &painter, const QRectF &plot )
{
    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    double b0, b1, b2, b3;
    computeDataBounds( &b0, &b1, &b2, &b3 );
    const double bounds[4] = { b0, b1, b2, b3 };
    const QColor accent = dark ? Tokens::Dark::accent : Tokens::Light::accent;

    const int n = static_cast<int>( std::min<qsizetype>(
        { m_data.series.xs.size(), m_data.series.ys.size(), kMaxDrawnPoints } ) );
    if ( n < 2 )
        return;
    QPolygonF poly;
    for ( int i = 0; i < n; ++i )
        poly.append( toPixel( m_data.series.xs.at( i ), m_data.series.ys.at( i ), plot, bounds ) );
    painter.setPen( QPen( accent, 1.6 ) );
    painter.setBrush( Qt::NoBrush );
    painter.drawPolyline( poly );

    if ( m_highlightIndex >= 0 && m_highlightIndex < n )
    {
        painter.setBrush( accent );
        painter.drawEllipse( poly.at( m_highlightIndex ), 3.5, 3.5 );
    }
}

void VaChartWidget::paintScatter( QPainter &painter, const QRectF &plot )
{
    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    double b0, b1, b2, b3;
    computeDataBounds( &b0, &b1, &b2, &b3 );
    const double bounds[4] = { b0, b1, b2, b3 };
    const QColor inkSecondary = dark ? Tokens::Dark::inkSecondary : Tokens::Light::inkSecondary;
    const QVector<QColor> palette = categoricalPalette( dark );

    const int n = static_cast<int>( std::min<qsizetype>(
        { m_data.scatter.xs.size(), m_data.scatter.ys.size(), kMaxDrawnPoints } ) );
    for ( int i = 0; i < n; ++i )
    {
        const int group = i < m_data.scatter.groups.size() ? m_data.scatter.groups.at( i ) : -1;
        painter.setPen( Qt::NoPen );
        painter.setBrush( group >= 0 && group < palette.size() ? palette.at( group )
                                                               : inkSecondary );
        painter.drawEllipse( toPixel( m_data.scatter.xs.at( i ), m_data.scatter.ys.at( i ), plot, bounds ), 2.2, 2.2 );
    }
    if ( m_highlightIndex >= 0 && m_highlightIndex < n )
    {
        painter.setPen( QPen( dark ? Tokens::Dark::inkPrimary : Tokens::Light::inkPrimary, 1.4 ) );
        painter.setBrush( Qt::NoBrush );
        painter.drawEllipse(
            toPixel( m_data.scatter.xs.at( m_highlightIndex ), m_data.scatter.ys.at( m_highlightIndex ), plot, bounds ),
            5.0, 5.0 );
    }
}

void VaChartWidget::paintBoxPlot( QPainter &painter, const QRectF &plot )
{
    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    double b0, b1, b2, b3;
    computeDataBounds( &b0, &b1, &b2, &b3 );
    const double bounds[4] = { b0, b1, b2, b3 };
    const QColor accent = dark ? Tokens::Dark::accent : Tokens::Light::accent;
    const QColor inkPrimary = dark ? Tokens::Dark::inkPrimary : Tokens::Light::inkPrimary;

    const int boxes = static_cast<int>( std::min<qsizetype>( m_data.boxPlot.boxes.size(),
                                                             kMaxDrawnBars ) );
    for ( int i = 0; i < boxes; ++i )
    {
        const VaBox &box = m_data.boxPlot.boxes.at( i );
        const double cx = b0 + ( i + 0.5 );
        const double w = ( b1 - b0 ) / boxes * 0.5;
        const QPointF q1 = toPixel( cx - w / 2, box.q1, plot, bounds );
        const QPointF q3 = toPixel( cx + w / 2, box.q3, plot, bounds );
        const QPointF q2 = toPixel( cx - w / 2, box.q2, plot, bounds );
        const QPointF lo = toPixel( cx, box.q0, plot, bounds );
        const QPointF hi = toPixel( cx, box.q4, plot, bounds );

        painter.setPen( QPen( inkPrimary, 1.2 ) );
        painter.drawLine( QPointF( ( q1.x() + q3.x() ) / 2, lo.y() ),
                          QPointF( ( q1.x() + q3.x() ) / 2, hi.y() ) );
        painter.setBrush( accent );
        painter.drawRect( QRectF( q1.x(), q3.y(), q3.x() - q1.x(), q1.y() - q3.y() ) );
        painter.setPen( QPen( inkPrimary, 1.6 ) );
        painter.drawLine( q2, QPointF( q3.x(), q2.y() ) );

        painter.setPen( Qt::NoPen );
        painter.setBrush( dark ? Tokens::Dark::err : Tokens::Light::err );
        const int outliers = std::min<qsizetype>( box.outliers.size(), kMaxDrawnPoints );
        for ( int oi = 0; oi < outliers; ++oi )
            painter.drawEllipse( toPixel( cx, box.outliers.at( oi ), plot, bounds ), 1.8, 1.8 );
    }
}

void VaChartWidget::paintMatrix( QPainter &painter, const QRectF &plot )
{
    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    const QColor accent = dark ? Tokens::Dark::accent : Tokens::Light::accent;
    const QColor inkPrimary = dark ? Tokens::Dark::inkPrimary : Tokens::Light::inkPrimary;

    const int rows = m_data.matrix.rowLabels.size();
    const int cols = m_data.matrix.colLabels.size();
    if ( rows <= 0 || cols <= 0 || rows * cols > kMaxDrawnMatrixCells )
        return;
    if ( m_data.matrix.cells.size() != rows * cols )
        return; // malformed payload: render nothing rather than assert
    const qint64 maxCell = *std::max_element( m_data.matrix.cells.cbegin(), m_data.matrix.cells.cend() );
    const double cellW = plot.width() / cols;
    const double cellH = plot.height() / rows;
    for ( int r = 0; r < rows; ++r )
    {
        for ( int c = 0; c < cols; ++c )
        {
            const qint64 v = m_data.matrix.cells.at( r * cols + c );
            const double t = maxCell > 0 ? static_cast<double>( v ) / maxCell : 0.0;
            QColor fill = accent;
            fill.setAlpha( 28 + static_cast<int>( t * 200.0 ) );
            painter.fillRect( QRectF( plot.left() + c * cellW, plot.top() + r * cellH,
                                      cellW - 1, cellH - 1 ),
                              fill );
            if ( cellW > 26 && cellH > 16 )
            {
                painter.setPen( inkPrimary );
                painter.drawText( QRectF( plot.left() + c * cellW, plot.top() + r * cellH,
                                          cellW, cellH ),
                                  Qt::AlignCenter, QString::number( v ) );
            }
        }
    }
}

void VaChartWidget::paintAreas( QPainter &painter, const QRectF &plot )
{
    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    const QVector<QColor> palette = categoricalPalette( dark );

    const int n = static_cast<int>( std::min<qsizetype>(
        { m_data.areas.labels.size(), m_data.areas.values.size(), kMaxDrawnBars } ) );
    if ( n == 0 )
        return;
    qint64 total = 0;
    for ( int i = 0; i < n; ++i )
        total += m_data.areas.values.at( i );
    if ( total <= 0 )
        return;

    double x = plot.left();
    b1 = 1;
    Q_UNUSED( b0 );
    Q_UNUSED( b2 );
    Q_UNUSED( b3 );
    for ( int i = 0; i < n; ++i )
    {
        const double w = plot.width() * static_cast<double>( m_data.areas.values.at( i ) ) / total;
        painter.setBrush( palette.at( i % palette.size() ) );
        painter.setPen( Qt::NoPen );
        painter.drawRect( QRectF( x, plot.top(), w, plot.height() ) );
        if ( w > 30 )
        {
            painter.setPen( dark ? Tokens::Dark::inkPrimary : Tokens::Light::inkPrimary );
            painter.drawText( QRectF( x, plot.bottom() + 2, w, 24 ), Qt::AlignHCenter | Qt::AlignTop,
                              m_data.areas.labels.at( i ) );
        }
        x += w;
    }
}

void VaChartWidget::paintAxes( QPainter &painter, const QRectF &plot )
{
    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    const QColor lineSubtle = dark ? Tokens::Dark::lineSubtle : Tokens::Light::lineSubtle;
    painter.setPen( QPen( lineSubtle, 1.0 ) );
    painter.drawRect( plot );
}

void VaChartWidget::paintNiceTicks( QPainter &painter, const QRectF &plot, const double bounds[4] )
{
    const bool dark = SicnuUi::Tokens::themeIsDark( this );
    const QColor inkSecondary = dark ? Tokens::Dark::inkSecondary : Tokens::Light::inkSecondary;
    const QColor lineSubtle = dark ? Tokens::Dark::lineSubtle : Tokens::Light::lineSubtle;
    painter.setPen( inkSecondary );

    if ( m_data.kind == VaChartKind::Matrix || m_data.kind == VaChartKind::Areas )
    {
        // Categorical axes: labels drawn by the painters themselves.
        return;
    }
    const QVector<double> xt = niceTicks( bounds[0], bounds[1], 6 );
    for ( double t : xt )
    {
        const QPointF p = toPixel( t, bounds[2], plot, bounds );
        painter.drawText( QRectF( p.x() - 40, plot.bottom() + 4, 80, 14 ),
                          Qt::AlignHCenter | Qt::AlignTop, formatNumber( t ) );
        painter.setPen( QPen( lineSubtle, 0.8 ) );
        painter.drawLine( QPointF( p.x(), plot.bottom() ), QPointF( p.x(), plot.bottom() + 3 ) );
        painter.setPen( inkSecondary );
    }
    const QVector<double> yt = niceTicks( bounds[2], bounds[3], 5 );
    for ( double t : yt )
    {
        const QPointF p = toPixel( bounds[0], t, plot, bounds );
        painter.drawText( QRectF( 4, p.y() - 7, plot.left() - 8, 14 ),
                          Qt::AlignRight | Qt::AlignVCenter, formatNumber( t ) );
        painter.setPen( QPen( lineSubtle, 0.8 ) );
        painter.drawLine( QPointF( plot.left() - 3, p.y() ), QPointF( plot.left(), p.y() ) );
        painter.setPen( inkSecondary );
    }
}

void VaChartWidget::mousePressEvent( QMouseEvent *event )
{
    double x = 0, y = 0;
    if ( m_mode == Mode::Ready && toData( event->pos(), &x, &y ) )
    {
        if ( m_data.kind == VaChartKind::Histogram || m_data.kind == VaChartKind::Series )
        {
            m_dragging = true;
            m_dragStart = x;
            m_dragCurrent = x;
            m_hasRange = true;
        }
        else if ( m_data.kind == VaChartKind::Scatter )
        {
            // nearest point within 8 px
            double b[4];
            double b0, b1, b2, b3;
            computeDataBounds( &b0, &b1, &b2, &b3 );
            b[0] = b0; b[1] = b1; b[2] = b2; b[3] = b3;
            const QRectF plot = plotRect();
            int best = -1;
            double bestDist = 64.0;
            const int n = static_cast<int>( std::min<qsizetype>(
                { m_data.scatter.xs.size(), m_data.scatter.ys.size(), kMaxDrawnPoints } ) );
            for ( int i = 0; i < n; ++i )
            {
                const QPointF p = toPixel( m_data.scatter.xs.at( i ), m_data.scatter.ys.at( i ), plot, b );
                const double d = std::pow( p.x() - event->pos().x(), 2 ) + std::pow( p.y() - event->pos().y(), 2 );
                if ( d < bestDist )
                {
                    bestDist = d;
                    best = i;
                }
            }
            if ( best >= 0 )
            {
                m_highlightIndex = best;
                update();
                emit pointSelected( best );
            }
        }
    }
    QWidget::mousePressEvent( event );
}

void VaChartWidget::mouseMoveEvent( QMouseEvent *event )
{
    if ( m_dragging )
    {
        double x = 0, y = 0;
        if ( toData( event->pos(), &x, &y ) )
        {
            m_dragCurrent = x;
            update();
        }
    }
    QWidget::mouseMoveEvent( event );
}

void VaChartWidget::mouseReleaseEvent( QMouseEvent *event )
{
    if ( m_dragging )
    {
        m_dragging = false;
        double x = 0, y = 0;
        if ( toData( event->pos(), &x, &y ) )
            m_dragCurrent = x;
        if ( std::abs( m_dragCurrent - m_dragStart ) > 1e-12 )
            emit rangeSelected( std::min( m_dragStart, m_dragCurrent ),
                                std::max( m_dragStart, m_dragCurrent ) );
        update();
    }
    QWidget::mouseReleaseEvent( event );
}

void VaChartWidget::keyPressEvent( QKeyEvent *event )
{
    if ( m_mode == Mode::Ready )
    {
        int count = 0;
        if ( m_data.kind == VaChartKind::BoxPlot )
            count = m_data.boxPlot.boxes.size();
        else if ( m_data.kind == VaChartKind::Areas )
            count = m_data.areas.labels.size();
        if ( count > 0 )
        {
            if ( event->key() == Qt::Key_Right || event->key() == Qt::Key_Down )
                m_highlightIndex = std::min( count - 1, m_highlightIndex + 1 );
            else if ( event->key() == Qt::Key_Left || event->key() == Qt::Key_Up )
                m_highlightIndex = std::max( 0, m_highlightIndex - 1 );
            else if ( event->key() == Qt::Key_Return && m_highlightIndex >= 0 )
                emit categorySelected( m_highlightIndex );
            update();
        }
    }
    QWidget::keyPressEvent( event );
}

void VaChartWidget::leaveEvent( QEvent *event )
{
    QWidget::leaveEvent( event );
}

QString VaChartWidget::toCsv() const
{
    if ( m_mode != Mode::Ready )
        return QString();
    switch ( m_data.kind )
    {
        case VaChartKind::Histogram:
        {
            QString csv = QStringLiteral( "bin_start,bin_end,count\n" );
            for ( int i = 0; i < m_data.histogram.counts.size(); ++i )
                csv += QStringLiteral( "%1,%2,%3\n" )
                           .arg( m_data.histogram.binEdges.value( i ), 0, 'g', 6 )
                           .arg( m_data.histogram.binEdges.value( i + 1 ), 0, 'g', 6 )
                           .arg( m_data.histogram.counts.at( i ) );
            return csv;
        }
        case VaChartKind::Series:
        {
            QString csv = QStringLiteral( "x,y\n" );
            for ( int i = 0; i < std::min( m_data.series.xs.size(), m_data.series.ys.size() ); ++i )
                csv += QStringLiteral( "%1,%2\n" )
                           .arg( m_data.series.xs.at( i ), 0, 'g', 6 )
                           .arg( m_data.series.ys.at( i ), 0, 'g', 6 );
            return csv;
        }
        case VaChartKind::Scatter:
        {
            QString csv = QStringLiteral( "x,y,group\n" );
            for ( int i = 0; i < std::min( m_data.scatter.xs.size(), m_data.scatter.ys.size() ); ++i )
            {
                const int g = i < m_data.scatter.groups.size() ? m_data.scatter.groups.at( i ) : -1;
                csv += QStringLiteral( "%1,%2,%3\n" )
                           .arg( m_data.scatter.xs.at( i ), 0, 'g', 6 )
                           .arg( m_data.scatter.ys.at( i ), 0, 'g', 6 )
                           .arg( g >= 0 && g < m_data.scatter.groupNames.size()
                                     ? m_data.scatter.groupNames.at( g )
                                     : QStringLiteral( "-" ) );
            }
            return csv;
        }
        default:
            return QString();
    }
}

QString VaChartWidget::toJson() const
{
    if ( m_mode != Mode::Ready )
        return QString();
    QJsonObject root;
    root[QStringLiteral( "kind" )] = static_cast<int>( m_data.kind );
    root[QStringLiteral( "generatedAt" )] =
        QDateTime::currentDateTimeUtc().toString( Qt::ISODate );
    switch ( m_data.kind )
    {
        case VaChartKind::Histogram:
            root[QStringLiteral( "bins" )] = m_data.histogram.counts.size();
            root[QStringLiteral( "validCount" )] = static_cast<qint64>( m_data.histogram.validCount );
            root[QStringLiteral( "noDataCount" )] = static_cast<qint64>( m_data.histogram.noDataCount );
            root[QStringLiteral( "mean" )] = m_data.histogram.mean;
            root[QStringLiteral( "stddev" )] = m_data.histogram.stddev;
            root[QStringLiteral( "truncated" )] = m_data.histogram.truncated;
            break;
        case VaChartKind::Matrix:
            root[QStringLiteral( "rows" )] = m_data.matrix.rowLabels.size();
            root[QStringLiteral( "cols" )] = m_data.matrix.colLabels.size();
            root[QStringLiteral( "total" )] = static_cast<qint64>( m_data.matrix.total() );
            break;
        case VaChartKind::Scatter:
            root[QStringLiteral( "points" )] = m_data.scatter.xs.size();
            root[QStringLiteral( "truncated" )] = m_data.scatter.truncated;
            break;
        default:
            break;
    }
    return QString::fromUtf8( QJsonDocument( root ).toJson( QJsonDocument::Indented ) );
}

} // namespace sicnu::app::va
