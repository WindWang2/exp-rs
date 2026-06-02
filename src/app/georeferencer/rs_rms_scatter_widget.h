#ifndef RS_RMS_SCATTER_WIDGET_H
#define RS_RMS_SCATTER_WIDGET_H

#include <QPointF>
#include <QVector>
#include <QWidget>

/**
 * Tiny 150x150 QPainter-based widget that scatter-plots GCP residuals.
 *
 * Coordinates fed via setResiduals() are interpreted in pixels (dx, dy).
 * A dashed warn-circle at the warn threshold (default 1.0 px) marks the
 * acceptable region; dots inside are drawn green (#208830), dots at or
 * outside are drawn amber (#bf8700).
 *
 * Used by RsGeorefParamsPanel section 3 (RMS 误差分布).
 */
class RsRmsScatterWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit RsRmsScatterWidget( QWidget *parent = nullptr );

    void setResiduals( const QVector<QPointF> &dxdy );
    void setWarnThreshold( double pixels );

    QSize sizeHint() const override { return QSize( 150, 150 ); }
    QSize minimumSizeHint() const override { return QSize( 150, 150 ); }

  protected:
    void paintEvent( QPaintEvent *e ) override;

  private:
    QVector<QPointF> mPts;
    double mWarnPx = 1.0;
};

#endif // RS_RMS_SCATTER_WIDGET_H
