#ifndef RS_SPECTRAL_CURVE_WIDGET_H
#define RS_SPECTRAL_CURVE_WIDGET_H

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

/**
 * Phase 10A — Task 10.5
 *
 * Per-class mean ± σ spectral curve widget. Each curve is a polyline of
 * band means with a translucent ±σ band drawn around it. The currently
 * selected class is drawn with a thicker pen.
 *
 * Public API is Qt-only (no QGIS dependency) so the widget can be unit
 * tested with just QApplication + QImage::render(). Raster sampling
 * (turning RsRoi pixel indices into per-band reflectance statistics) is
 * the caller's job — wiring lands in Task 10.8.
 *
 * Mirrors the pattern of RsRmsScatterWidget (Phase 11.4.7).
 */
class RsSpectralCurveWidget : public QWidget
{
    Q_OBJECT
  public:
    /**
     * One curve = one class. bandMeans and bandStds are aligned per band
     * index. bandStds may be shorter than bandMeans (or empty); missing
     * entries are treated as 0 standard deviation.
     */
    struct Curve
    {
      int classId = 0;
      QColor color;
      QString name;
      QVector<double> bandMeans;
      QVector<double> bandStds;
    };

    explicit RsSpectralCurveWidget( QWidget *parent = nullptr );

    void setClassCurves( const QVector<Curve> &curves );
    void setSelectedClass( int classId );

    QSize sizeHint() const override { return QSize( 400, 180 ); }
    QSize minimumSizeHint() const override { return QSize( 360, 140 ); }

  protected:
    void paintEvent( QPaintEvent *e ) override;

  private:
    QVector<Curve> mCurves;
    int mSelected = -1;
};

#endif // RS_SPECTRAL_CURVE_WIDGET_H
