#pragma once

#include <QVector>
#include <QWidget>

namespace sicnu::app::experiment_studio
{

struct ChartSeriesPoint
{
    double x = 0.0;
    double y = 0.0;
    double yMin = 0.0;
    double yMax = 0.0;
};

/// Lightweight Qt-native chart (QPainter). No web frontend / QtCharts dep.
class SensitivityChartWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit SensitivityChartWidget( QWidget *parent = nullptr );

    void setSeries( const QString &title, const QVector<ChartSeriesPoint> &points, bool showBand );
    void clearSeries();

  protected:
    void paintEvent( QPaintEvent *event ) override;

  private:
    QString m_title;
    QVector<ChartSeriesPoint> m_points;
    bool m_showBand = false;
};

} // namespace sicnu::app::experiment_studio
