// src/app/widgets/histogram_widget.h
#pragma once

#include <QWidget>
#include <QPointer>
#include <QVector>
#include <QPointF>
#include <QString>
#include <QColor>

#include <qgsrasterlayer.h>

typedef void *GDALDatasetH;

/**
 * \brief Interactive Photoshop-style Raster Histogram Widget.
 *
 * Supports Real Physical Data Ranges (16-bit / 32-bit Float), Multi-channel RGB/Grayscale,
 * Photoshop Level Cut-off Handles (Black, Gamma, White), and Piecewise Linear Stretch (分段线性拉伸)
 * with interactive control point handles ($\bigcirc$).
 */
class HistogramWidget : public QWidget
{
    Q_OBJECT

public:
    enum class ChannelMode
    {
        MasterRGB,
        Red,
        Green,
        Blue,
        SingleBand
    };

    explicit HistogramWidget( QWidget *parent = nullptr );
    ~HistogramWidget() override;

    void setRasterLayer( QgsRasterLayer *layer );
    void setBand( int bandNumber );

    void setChannelMode( ChannelMode mode );
    ChannelMode channelMode() const { return m_channelMode; }

    void setRgbBands( int rBand, int gBand, int bBand );

    double blackCutoff() const { return m_blackCutoff; }
    double whiteCutoff() const { return m_whiteCutoff; }
    double gamma() const { return m_gamma; }

    void setCutoffs( double black, double white, double gamma );

    // Piecewise Linear Stretch Control Points
    bool isPiecewiseEnabled() const { return m_enablePiecewise; }
    void setEnablePiecewise( bool enable );
    void setPiecewisePoints( const QVector<QPointF> &points );
    void resetPiecewisePoints();
    QVector<QPointF> piecewisePoints() const { return m_piecewisePoints; }

    QgsRasterLayer *rasterLayer() const { return m_rasterLayer.data(); }
    int band() const { return m_band; }

    double realDataMin() const { return activeBandData().minVal; }
    double realDataMax() const { return activeBandData().maxVal; }

    QSize minimumSizeHint() const override { return QSize( 340, 240 ); }
    QSize sizeHint() const override { return QSize( 540, 360 ); }

Q_SIGNALS:
    void cutoffsChanged( double black, double white, double gamma );
    void piecewisePointsChanged( const QVector<QPointF> &points );

protected:
    void paintEvent( QPaintEvent *event ) override;
    void mousePressEvent( QMouseEvent *event ) override;
    void mouseMoveEvent( QMouseEvent *event ) override;
    void mouseReleaseEvent( QMouseEvent *event ) override;
    void mouseDoubleClickEvent( QMouseEvent *event ) override;

private:
    struct BandData
    {
        QVector<double> histogram;
        int binCount = 0;
        double minVal = 0.0;
        double maxVal = 255.0;
        double maxFreq = 0.0;
        double mean = 0.0;
        double stddev = 0.0;
        bool valid = false;
    };

    enum class ActiveHandle
    {
        None,
        Black,
        Gamma,
        White,
        PiecewisePoint
    };

    void computeHistograms();
    void drawChart( QPainter &painter, const QRect &chartRect );
    void drawAxes( QPainter &painter, const QRect &chartRect );
    void drawBars( QPainter &painter, const QRect &chartRect );
    void drawHandles( QPainter &painter, const QRect &chartRect );
    void drawPiecewiseCurve( QPainter &painter, const QRect &chartRect );
    void drawStats( QPainter &painter, const QRect &statsRect );
    void closeDataset();
    const BandData &activeBandData() const;

    QRect getChartRect() const;
    ActiveHandle hitTestHandle( const QPoint &pos ) const;
    int hitTestPiecewisePoint( const QPoint &pos ) const;

    double valToX( double val, const QRect &chartRect ) const;
    double xToVal( int x, const QRect &chartRect ) const;
    double displayToY( double displayVal, const QRect &chartRect ) const;
    double yToDisplay( int y, const QRect &chartRect ) const;

    QPointer<QgsRasterLayer> m_rasterLayer;
    int m_band = 1;
    int m_redBand = 1;
    int m_greenBand = 2;
    int m_blueBand = 3;

    ChannelMode m_channelMode = ChannelMode::SingleBand;

    GDALDatasetH m_cachedDataset = nullptr;
    QString m_cachedSource;
    QMap<int, BandData> m_bandCache;

    BandData m_singleBandData;
    BandData m_redData;
    BandData m_greenData;
    BandData m_blueData;

    // Interactive Level cut-offs (in physical pixel value space)
    double m_blackCutoff = 0.0;
    double m_whiteCutoff = 255.0;
    double m_gamma = 1.0;

    // Piecewise Linear Stretch Control Points (x = pixel value, y = output 0..255)
    bool m_enablePiecewise = false;
    QVector<QPointF> m_piecewisePoints;
    int m_selectedPiecewiseIndex = -1;

    ActiveHandle m_activeHandle = ActiveHandle::None;
    bool m_isDragging = false;
};
