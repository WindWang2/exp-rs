/***************************************************************************
  app/workbench/temporal_timeline_widget.h
  Temporal Phenology Timeline Studio (D16) — temporal profile chart and the
  timeline workbench composite.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  TemporalProfileWidget renders raw observations (scatter), the smoothed
  curve, the phenology season band (SOS..EOS with the POS marker) and
  harmonic-break markers. Two-layer rendering: the static chrome (grid,
  axes, band, curves) is cached in a QPixmap and invalidated only when the
  data or geometry changes; the scrubber hover cursor is drawn live on top
  (D16 §G double-buffering contract — 60 fps scrubbing never replots data).

  TemporalTimelineWidget composes the profile chart with the scrubber and
  forwards slice changes to both.
 ***************************************************************************/

#ifndef SICNU_APP_WORKBENCH_TEMPORAL_TIMELINE_WIDGET_H
#define SICNU_APP_WORKBENCH_TEMPORAL_TIMELINE_WIDGET_H

#include "app/widgets/timeline_scrubber_widget.h"

#include <QPixmap>
#include <QWidget>

#include <vector>

class QVBoxLayout;

namespace sicnu::gui
{

class TemporalProfileWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit TemporalProfileWidget( QWidget *parent = nullptr );
    ~TemporalProfileWidget() override = default;

    void setRawObservations( const std::vector<double> &tDays,
                             const std::vector<float> &values );
    void setSmoothedCurve( const std::vector<double> &tDays,
                           const std::vector<float> &values );
    /// Season band on the tDays axis; NaN or sos >= eos hides the band.
    void setPhenologyInterval( double sos, double pos, double eos );
    void setBreakpoints( const std::vector<double> &breakDays );
    /// Moves the scrubber cursor (dynamic layer only).
    void setScrubberHoverDate( double tDays );

  signals:
    /// Nearest raw observation to the cursor (NaN-free contract: only
    /// emitted when a raw sample is within 20 px of the cursor).
    void sampleHovered( double tDays, float value );

  protected:
    void paintEvent( QPaintEvent *event ) override;
    void mouseMoveEvent( QMouseEvent *event ) override;
    void resizeEvent( QResizeEvent *event ) override;

  private:
    void rebuildBackground();
    void refreshAxes();
    QRectF plotRect() const;
    QPointF toPixel( double tDays, double value ) const;

    std::vector<double> mRawT;
    std::vector<float> mRawV;
    std::vector<double> mSmoothT;
    std::vector<float> mSmoothV;
    double mSos = 0.0;
    double mPos = 0.0;
    double mEos = 0.0;
    bool mHasPhenology = false;
    std::vector<double> mBreaks;
    double mHoverDays = 0.0;
    bool mHasHover = false;

    QPixmap mBackground;   // static chrome cache
    bool mBackgroundDirty = true;
    double mAxisT0 = 0.0;  // cached axis ranges (refreshAxes)
    double mAxisT1 = 0.0;
    double mAxisV0 = 0.0;
    double mAxisV1 = 0.0;
};

/// Workbench composite: profile on top, scrubber below.
class TemporalTimelineWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit TemporalTimelineWidget( QWidget *parent = nullptr );
    ~TemporalTimelineWidget() override = default;

    /// Installs the cube's timeline: one ISO date per slice with the
    /// matching day offset (the profile's cursor axis).
    void setTimeline( const std::vector<QString> &isoDates,
                      const std::vector<double> &sliceDays );

    TimelineScrubberWidget *scrubber() const { return mScrubber; }
    TemporalProfileWidget *profile() const { return mProfile; }

  private:
    void onSliceChanged( int index, const QString &isoDate );

    TimelineScrubberWidget *mScrubber = nullptr;
    TemporalProfileWidget *mProfile = nullptr;
    QVBoxLayout *mLayout = nullptr;
    std::vector<double> mSliceDays;
};

} // namespace sicnu::gui

#endif // SICNU_APP_WORKBENCH_TEMPORAL_TIMELINE_WIDGET_H
