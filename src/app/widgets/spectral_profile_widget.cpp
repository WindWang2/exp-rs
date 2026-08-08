// src/app/widgets/spectral_profile_widget.cpp
#include "spectral_profile_widget.h"
#include "core/sicnu_logging.h"
#include "processing/algorithms/spectral_classification.h"

#include <raster/qgsrasterlayer.h>
#include <raster/qgsrasterdataprovider.h>
#include <qgsproject.h>

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_error.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{

/// Shared numeric label formatter for the value labels and the Y-axis labels
/// (kept in one place so the two paint paths cannot drift).
QString formatValue( double v )
{
    if ( std::abs( v ) < 1.0 && v != 0.0 )
        return QString::number( v, 'g', 3 );
    if ( std::abs( v ) >= 1000.0 )
        return QString::number( v, 'f', 0 );
    return QString::number( v, 'g', 5 );
}

} // namespace
#include <limits>

// ── Constructor / Destructor ─────────────────────────────────────────────────

SpectralProfileWidget::SpectralProfileWidget( QWidget *parent )
    : QWidget( parent )
{
    setMinimumSize( 320, 220 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

    // Connect to layer removal to clear dangling pointer and close dataset
    connect( QgsProject::instance(), &QgsProject::layerRemoved,
             this, [this]( const QString &layerId ) {
                 if ( m_rasterLayer && m_rasterLayer->id() == layerId ) {
                     m_rasterLayer = nullptr;
                     closeDataset();
                     clear();
                 }
             } );
}

SpectralProfileWidget::~SpectralProfileWidget()
{
    closeDataset();
}

void SpectralProfileWidget::closeDataset()
{
    if ( m_cachedDataset ) {
        GDALClose( m_cachedDataset );
        m_cachedDataset = nullptr;
        m_cachedSource.clear();
    }
}

// ── Public methods ───────────────────────────────────────────────────────────

void SpectralProfileWidget::setProfile( const QgsPointXY &point, QgsRasterLayer *layer )
{
    m_point = point;
    m_rasterLayer = layer;
    extractProfile( point, layer );
    update();
}

void SpectralProfileWidget::setSpectrum( const QVector<double> &values,
                                         const QVector<double> &wavelengths,
                                         const QVector<QString> &labels,
                                         const QString &layerName )
{
    // Display a precomputed spectrum (e.g. an ROI mean spectrum from the
    // SpectralRoiProfile kernel) in the same chart as a point profile.
    m_values = values;
    m_wavelengths = wavelengths.size() == values.size() ? wavelengths : QVector<double>();
    m_bandLabels = labels.size() == values.size() ? labels : QVector<QString>();
    m_fwhm.fill( 0.0, values.size() ); // ROI spectra carry no FWHM source
    m_layerName = layerName;
    m_hasData = !values.isEmpty();

    // The chart's Y range derives from the displayed values (extractProfile
    // is not involved for precomputed spectra); reset on the raw range.
    m_minValue = 0.0;
    m_maxValue = 0.0;
    for ( double v : values )
    {
        if ( !std::isfinite( v ) )
            continue;
        m_minValue = (std::min)( m_minValue, v );
        m_maxValue = (std::max)( m_maxValue, v );
    }
    if ( m_minValue >= m_maxValue )
        m_maxValue = m_minValue + 1.0;

    m_displayDirty = true;
    update();
}

void SpectralProfileWidget::setContinuumRemovalEnabled( bool enabled )
{
    if ( m_continuumRemoval == enabled )
        return;
    m_continuumRemoval = enabled;
    m_displayDirty = true;
    update();
}

void SpectralProfileWidget::plotRange( double *vMin, double *vMax ) const
{
    if ( m_continuumRemoval )
    {
        // Continuum-removed values lie in (0, 1].
        *vMin = 0.0;
        *vMax = 1.0;
        return;
    }
    *vMin = m_minValue;
    *vMax = m_maxValue;
}

QVector<double> SpectralProfileWidget::displayValues() const
{
    if ( !m_continuumRemoval )
        return m_values;
    if ( !m_displayDirty )
        return m_displayCache;
    if ( m_values.isEmpty() )
        return m_values;

    // Continuum removal kernel: reflectance / convex-hull continuum per band,
    // in (0, 1]. NaN bands are mapped to the nodata sentinel for the kernel.
    std::vector<float> raw;
    raw.reserve( m_values.size() );
    for ( double v : m_values )
        raw.push_back( std::isnan( v ) ? SpectralClassification::kNoDataSentinel
                                : static_cast<float>( v ) );
    std::vector<float> cr( raw.size() );
    if ( !SpectralClassification::continuumRemoval( raw.data(), cr.data(),
                                                    static_cast<int>( raw.size() ),
                                                    SpectralClassification::kNoDataSentinel ) )
        return m_values;

    m_displayCache.clear();
    m_displayCache.reserve( cr.size() );
    for ( float v : cr )
        m_displayCache.append( std::isnan( v ) ? std::numeric_limits<double>::quiet_NaN()
                                               : static_cast<double>( v ) );
    m_displayDirty = false;
    return m_displayCache;
}

void SpectralProfileWidget::clear()
{
    m_values.clear();
    m_bandLabels.clear();
    m_wavelengths.clear();
    m_fwhm.clear();
    m_layerName.clear();
    m_hasData = false;
    m_rasterLayer = nullptr;
    m_minValue = 0.0;
    m_maxValue = 0.0;
    m_displayDirty = true;
    m_displayCache.clear();
    update();
}

// ── GDAL extraction ──────────────────────────────────────────────────────────

double SpectralProfileWidget::xFractionForBand( int i, int bandCount ) const
{
    if ( bandCount > 1 && m_wavelengths.size() == static_cast<int>( bandCount ) )
    {
        double minWl = m_wavelengths[0];
        double maxWl = m_wavelengths[0];
        bool allValid = true;
        for ( double w : m_wavelengths )
        {
            if ( w <= 0.0 || !std::isfinite( w ) )
            {
                allValid = false;
                break;
            }
            minWl = (std::min)( minWl, w );
            maxWl = (std::max)( maxWl, w );
        }
        if ( allValid && maxWl > minWl )
            return ( m_wavelengths[i] - minWl ) / ( maxWl - minWl );
    }
    return bandCount > 1
        ? static_cast<double>( i ) / ( bandCount - 1 )
        : 0.5;
}

void SpectralProfileWidget::extractProfile( const QgsPointXY &point, QgsRasterLayer *layer )
{
    m_values.clear();
    m_bandLabels.clear();
    m_wavelengths.clear();
    m_hasData = false;
    m_minValue = (std::numeric_limits<double>::max)();
    m_maxValue = std::numeric_limits<double>::lowest();

    if ( !layer )
        return;

    SICNU_LOG_INFO( SicnuLogTags::Widgets, QString( "Extracting spectral profile: layer=%1, point=(%2, %3)" )
        .arg( layer->name() ).arg( point.x(), 0, 'f', 4 ).arg( point.y(), 0, 'f', 4 ) );

    m_layerName = layer->name();

    // Use cached dataset if the source hasn't changed
    const QString source = layer->source();
    if ( !m_cachedDataset || m_cachedSource != source ) {
        closeDataset();
        m_cachedDataset = GDALOpen( source.toUtf8().constData(), GA_ReadOnly );
        m_cachedSource = source;
    }

    GDALDatasetH dataset = m_cachedDataset;
    if ( !dataset )
        return;

    const int bandCount = GDALGetRasterCount( dataset );
    if ( bandCount < 1 )
        return;

    // Convert map coordinate (layer CRS) to pixel coordinate
    double adfGeoTransform[6];
    if ( GDALGetGeoTransform( dataset, adfGeoTransform ) != CE_None )
        return;

    double x = point.x();
    double y = point.y();
    double col = ( x - adfGeoTransform[0] ) / adfGeoTransform[1];
    double row = ( y - adfGeoTransform[3] ) / adfGeoTransform[5];

    // Check bounds
    const int nXSize = GDALGetRasterXSize( dataset );
    const int nYSize = GDALGetRasterYSize( dataset );
    if ( col < 0 || col >= nXSize || row < 0 || row >= nYSize )
        return;

    const int pixelX = static_cast<int>( col );
    const int pixelY = static_cast<int>( row );

    m_values.resize( bandCount );
    m_bandLabels.resize( bandCount );
    m_wavelengths.resize( bandCount );
    m_fwhm.resize( bandCount );

    for ( int i = 0; i < bandCount; ++i )
    {
        GDALRasterBandH band = GDALGetRasterBand( dataset, i + 1 );

        double pixelValue = 0.0;
        CPLErr err = GDALRasterIO( band, GF_Read,
                                   pixelX, pixelY, 1, 1,
                                   &pixelValue, 1, 1,
                                   GDT_Float64, 0, 0 );

        if ( err == CE_None )
        {
            m_values[i] = pixelValue;
            if ( pixelValue < m_minValue ) m_minValue = pixelValue;
            if ( pixelValue > m_maxValue ) m_maxValue = pixelValue;
        }
        else
        {
            m_values[i] = std::numeric_limits<double>::quiet_NaN();
        }

        const char *desc = GDALGetDescription( band );
        if ( desc && desc[0] != '\0' )
            m_bandLabels[i] = QString::fromUtf8( desc );
        else
            m_bandLabels[i] = QObject::tr( "Band %1" ).arg( i + 1 );

        // Wavelength-aware X axis: read the band's WAVELENGTH metadata (nm)
        // written by product stacking (ADR 0065); 0 when absent.
        const char *wl = GDALGetMetadataItem( band, "WAVELENGTH", nullptr );
        bool wlOk = false;
        const double wlValue = wl ? QString::fromUtf8( wl ).toDouble( &wlOk ) : 0.0;
        m_wavelengths[i] = ( wlOk && wlValue > 0.0 ) ? wlValue : 0.0;

        // FWHM metadata (nm), 0 when absent (D1 surface for library matching).
        const char *fwhm = GDALGetMetadataItem( band, "FWHM", nullptr );
        bool fwhmOk = false;
        const double fwhmValue = fwhm ? QString::fromUtf8( fwhm ).toDouble( &fwhmOk ) : 0.0;
        m_fwhm[i] = ( fwhmOk && fwhmValue > 0.0 ) ? fwhmValue : 0.0;
    }

    bool anyValid = false;
    for ( int i = 0; i < bandCount; ++i )
    {
        if ( !std::isnan( m_values[i] ) ) { anyValid = true; break; }
    }

    m_hasData = anyValid;
    if ( m_minValue >= m_maxValue )
        m_maxValue = m_minValue + 1.0;
    m_displayDirty = true;

    SICNU_LOG_DEBUG( SicnuLogTags::Widgets, QString( "Spectral profile extracted: %1 bands, hasData=%2" )
        .arg( bandCount ).arg( anyValid ) );
}

// ── Painting ─────────────────────────────────────────────────────────────────

void SpectralProfileWidget::paintEvent( QPaintEvent *event )
{
    Q_UNUSED( event );

    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.fillRect( rect(), QColor( 250, 250, 252 ) );

    const int margin = 50;
    const int bottomMargin = 50;

    QRect chartRect( margin, 30, width() - margin - 20, height() - 30 - bottomMargin );

    if ( !m_hasData )
    {
        painter.setPen( QColor( 160, 160, 160 ) );
        painter.setFont( QFont( "sans-serif", 11 ) );
        painter.drawText( rect(), Qt::AlignCenter,
                          m_rasterLayer
                              ? tr( "No valid pixel data at this location" )
                              : tr( "Click on a raster layer to view spectral profile" ) );
        return;
    }

    // Title
    painter.setPen( QColor( 50, 50, 50 ) );
    painter.setFont( QFont( "sans-serif", 10, QFont::Bold ) );
    QString title = tr( "Spectral Profile — %1" ).arg( m_layerName );
    painter.drawText( QRect( margin, 0, width() - margin - 20, 18 ),
                      Qt::AlignLeft | Qt::AlignVCenter, title );

    // Subtitle with coordinates
    painter.setPen( QColor( 120, 120, 120 ) );
    painter.setFont( QFont( "sans-serif", 8 ) );
    QString subtitle = tr( "Point: (%1, %2)" )
                           .arg( m_point.x(), 0, 'f', 4 )
                           .arg( m_point.y(), 0, 'f', 4 );
    painter.drawText( QRect( margin, 16, width() - margin - 20, 14 ),
                      Qt::AlignLeft | Qt::AlignVCenter, subtitle );

    drawChart( painter, chartRect );
}

void SpectralProfileWidget::drawChart( QPainter &painter, const QRect &chartRect )
{
    // Background
    painter.fillRect( chartRect, QColor( 255, 255, 255 ) );
    painter.setPen( QPen( QColor( 200, 200, 200 ), 1 ) );
    painter.drawRect( chartRect.adjusted( 0, 0, -1, -1 ) );

    // Horizontal grid lines
    painter.setPen( QPen( QColor( 230, 230, 230 ), 1, Qt::DashLine ) );
    const int gridLines = 4;
    for ( int i = 1; i <= gridLines; ++i )
    {
        int y = chartRect.top() + chartRect.height() * i / ( gridLines + 1 );
        painter.drawLine( chartRect.left(), y, chartRect.right(), y );
    }

    drawLine( painter, chartRect );
    drawAxes( painter, chartRect );
}

void SpectralProfileWidget::drawLine( QPainter &painter, const QRect &chartRect )
{
    const QVector<double> values = displayValues();
    const int bandCount = values.size();
    if ( bandCount < 1 )
        return;

    // Continuum-removed values lie in (0, 1]; raw values use the extracted
    // data range. Both drawLine and drawAxes must agree (plotRange).
    double vMin = 0.0, vMax = 1.0;
    plotRange( &vMin, &vMax );
    const double valueRange = vMax - vMin;
    if ( valueRange <= 0.0 )
        return;

    // Per-band X fractions (wavelength-scaled when metadata is present) are
    // computed once and reused by the marker loop, the value-label loop, and
    // the axis labels so annotations stay aligned with their markers.
    QVector<double> xFracs( bandCount );
    for ( int i = 0; i < bandCount; ++i )
        xFracs[i] = xFractionForBand( i, bandCount );

    // Calculate point positions. Bands whose read failed carry NaN (and inf
    // is not drawable either): they get no point and the polyline breaks
    // across the gap (a NaN cast to int is undefined behavior and would paint
    // a garbage segment).
    QVector<QPoint> points;
    points.reserve( bandCount );

    for ( int i = 0; i < bandCount; ++i )
    {
        if ( !std::isfinite( values[i] ) )
            continue;
        double yFrac = ( values[i] - vMin ) / valueRange;

        int px = chartRect.left() + static_cast<int>( xFracs[i] * chartRect.width() );
        int py = chartRect.bottom() - static_cast<int>( yFrac * chartRect.height() );

        points.append( QPoint( px, py ) );
    }

    if ( points.isEmpty() )
        return;

    // Draw the connecting line
    if ( points.size() > 1 )
    {
        QPen linePen( QColor( 66, 133, 244 ), 2, Qt::SolidLine );
        painter.setPen( linePen );
        for ( int i = 0; i < points.size() - 1; ++i )
        {
            painter.drawLine( points[i], points[i + 1] );
        }
    }

    // Draw point markers and value labels (NaN bands have no point)
    painter.setFont( QFont( "sans-serif", 7 ) );
    for ( int i = 0; i < points.size(); ++i )
    {
        // Marker
        painter.setPen( QPen( QColor( 50, 110, 210 ), 1 ) );
        painter.setBrush( QColor( 66, 133, 244 ) );
        painter.drawEllipse( points[i], 4, 4 );
    }
    for ( int i = 0; i < bandCount; ++i )
    {
        // Value label above the point, aligned with the marker (same X
        // fraction and same Y mapping as the point loop).
        if ( std::isfinite( values[i] ) )
        {
            const int px = chartRect.left() + static_cast<int>( xFracs[i] * chartRect.width() );
            const int py = chartRect.bottom() - static_cast<int>(
                ( ( values[i] - vMin ) / valueRange ) * chartRect.height() );
            painter.setPen( QColor( 60, 60, 60 ) );
            const QString valText = formatValue( values[i] );
            QFontMetrics fm( painter.font() );
            int tw = fm.horizontalAdvance( valText );
            int labelY = py - 8;
            if ( labelY < chartRect.top() + 4 )
                labelY = py + 14;
            painter.drawText( px - tw / 2, labelY, valText );
        }
    }
}

void SpectralProfileWidget::drawAxes( QPainter &painter, const QRect &chartRect )
{
    painter.setPen( QColor( 80, 80, 80 ) );
    painter.setFont( QFont( "sans-serif", 8 ) );
    const QFontMetrics fm( painter.font() );

    const int bandCount = m_bandLabels.size();

    // X axis: band labels (rotated if many bands)
    if ( bandCount > 0 )
    {
        const int xLabelY = chartRect.bottom() + 4;

        // Decide whether to show all labels or a subset
        bool showAll = bandCount <= 8;
        int step = showAll ? 1 : (std::max)( 1, bandCount / 8 );

        for ( int i = 0; i < bandCount; i += step )
        {
            double xFrac = xFractionForBand( i, bandCount );
            int x = chartRect.left() + static_cast<int>( xFrac * chartRect.width() );

            QString label = m_bandLabels[i];
            // Truncate long labels
            if ( label.size() > 10 )
                label = label.left( 9 ) + QStringLiteral( "..." );

            int labelW = fm.horizontalAdvance( label );

            painter.save();
            if ( bandCount > 6 )
            {
                // Rotate labels for readability
                painter.translate( x, xLabelY + 4 );
                painter.rotate( 45 );
                painter.drawText( 0, 0, label );
            }
            else
            {
                painter.drawText( x - labelW / 2, xLabelY + fm.ascent(), label );
            }
            painter.restore();
        }

        // X axis title: wavelength-scaled when the bands carry WAVELENGTH
        // metadata, else band-indexed.
        painter.setFont( QFont( "sans-serif", 9 ) );
        const QFontMetrics fmTitle( painter.font() );
        const bool wavelengthAxis = ( m_wavelengths.size() == static_cast<int>( bandCount ) )
                                    && std::none_of( m_wavelengths.begin(), m_wavelengths.end(),
                                                     []( double w ) { return w <= 0.0; } );
        QString xTitle = wavelengthAxis ? tr( "波长 Wavelength (nm)" ) : tr( "Band" );
        int xTitleX = chartRect.left() + ( chartRect.width() - fmTitle.horizontalAdvance( xTitle ) ) / 2;
        int xTitleY = chartRect.bottom() + 4 + ( bandCount > 6 ? 40 : fm.height() + 4 );
        painter.drawText( xTitleX, xTitleY, xTitle );
    }

    // Y axis: value labels. The range must match drawLine's plotRange so the
    // axis describes the curve actually plotted (raw range or CR [0, 1]).
    double vMin = 0.0, vMax = 1.0;
    plotRange( &vMin, &vMax );
    if ( vMax > vMin )
    {
        // Top (max)
        QString topLabel = formatValue( vMax );
        painter.drawText( chartRect.left() - fm.horizontalAdvance( topLabel ) - 6,
                          chartRect.top() + fm.ascent(), topLabel );

        // Bottom (min)
        QString botLabel = formatValue( vMin );
        painter.drawText( chartRect.left() - fm.horizontalAdvance( botLabel ) - 6,
                          chartRect.bottom() + fm.ascent(), botLabel );

        // Mid value
        double midValue = ( vMin + vMax ) / 2.0;
        int midY = chartRect.top() + chartRect.height() / 2;
        QString midLabel = formatValue( midValue );
        painter.drawText( chartRect.left() - fm.horizontalAdvance( midLabel ) - 6,
                          midY + fm.ascent() / 2, midLabel );
    }

    // Y axis title (rotated)
    painter.save();
    painter.setFont( QFont( "sans-serif", 9 ) );
    const QFontMetrics fmTitle( painter.font() );
    QString yTitle = m_continuumRemoval ? tr( "Continuum Removed" ) : tr( "Pixel Value" );
    int yTitleX = 10;
    int yTitleY = chartRect.top() + ( chartRect.height() + fmTitle.horizontalAdvance( yTitle ) ) / 2;
    painter.translate( yTitleX, yTitleY );
    painter.rotate( -90 );
    painter.drawText( 0, 0, yTitle );
    painter.restore();
}
