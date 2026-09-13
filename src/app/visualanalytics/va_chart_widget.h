/***************************************************************************
 * va_chart_widget.h — Workbench 10.0 VA chart host
 *
 * One QPainter-drawn widget hosting every platform chart family (histogram,
 * series, scatter, box plot, matrix heatmap, class-area bars). Colors come
 * from SicnuUi::Tokens in both themes (no local QColor literals); every
 * state (empty/loading/error/ready) renders truthfully; the widget stays
 * keyboard-reachable for category selection.
 *
 * Bounded by construction: payloads are pre-bounded by their sources; the
 * painter additionally caps drawn points/pixels per chart so a hostile
 * payload cannot stall the UI thread.
 *
 * Selection (brushing): drag on a histogram/series selects an x-range;
 * click on scatter picks a point; click/keyboard on bars/boxes/picks a
 * category. Selection emits typed signals a VaSelectionHub fans out.
 ***************************************************************************/
#pragma once

#include "va_data.h"

#include <QWidget>

namespace sicnu::app::va
{

class VaChartWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit VaChartWidget( QWidget *parent = nullptr );

    /// Sets the shown payload (switches the family). Clears error/loading.
    void setData( const VaData &data );
    void setLoading( const QString &message = QString() );
    void setError( const QString &message );
    void clear();

    VaChartKind kind() const { return m_data.kind; }
    const VaData &data() const { return m_data; }

    /// Export of the CURRENT payload (bounded); empty string when nothing
    /// meaningful. CSV: one row family per chart; JSON: the raw payload.
    QString toCsv() const;
    QString toJson() const;

    QSize minimumSizeHint() const override { return { 220, 160 }; }

  signals:
    /// Histogram/series drag selection, in data coordinates.
    void rangeSelected( double x0, double x1 );
    /// Scatter point picked (payload index).
    void pointSelected( int index );
    /// Bar/box/matrix-row category picked (payload index).
    void categorySelected( int index );

  protected:
    void paintEvent( QPaintEvent *event ) override;
    void mousePressEvent( QMouseEvent *event ) override;
    void mouseMoveEvent( QMouseEvent *event ) override;
    void mouseReleaseEvent( QMouseEvent *event ) override;
    void keyPressEvent( QKeyEvent *event ) override;
    void leaveEvent( QEvent *event ) override;

  private:
    enum class Mode
    {
        Empty,
        Loading,
        Error,
        Ready,
    };

    // plot geometry helpers (device px → data coords and back)
    QRectF plotRect() const;
    void computeDataBounds( double *x0, double *x1, double *y0, double *y1 ) const;
    QPointF toPixel( double x, double y, const QRectF &plot, const double bounds[4] ) const;
    bool toData( const QPoint &pixel, double *x, double *y ) const;

    void paintHistogram( QPainter &painter, const QRectF &plot );
    void paintSeries( QPainter &painter, const QRectF &plot );
    void paintScatter( QPainter &painter, const QRectF &plot );
    void paintBoxPlot( QPainter &painter, const QRectF &plot );
    void paintMatrix( QPainter &painter, const QRectF &plot );
    void paintAreas( QPainter &painter, const QRectF &plot );
    void paintAxes( QPainter &painter, const QRectF &plot );
    void paintState( QPainter &painter, const QString &text );

    void paintNiceTicks( QPainter &painter, const QRectF &plot, const double bounds[4] );
    void drawLegendText( QPainter &painter, const QRectF &plot );

    VaData m_data;
    Mode m_mode = Mode::Empty;
    QString m_message;

    // brushing state (histogram/series only)
    bool m_dragging = false;
    double m_dragStart = 0;
    double m_dragCurrent = 0;
    bool m_hasRange = false;
    int m_highlightIndex = -1; ///< hovered/c keyboard-selected category or point
};

} // namespace sicnu::app::va
