/***************************************************************************
  app/widgets/timeline_scrubber_widget.h
  Temporal Phenology Timeline Studio (D16) — timeline scrubber.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Playback state machine (Paused / Playing / Seeking) over a list of
  acquisition dates. The play timer pulses at 60 fps (16 ms) and advances
  the slice index at framesPerSlice / playSpeed; reaching the end emits
  playbackFinished() and pauses. Dragging snaps to an acquisition tick
  within 5 px (snap-to-acquisition).

  Signal contract: dateChanged(index, isoDate) fires on every committed
  index change (programmatic or interactive); playbackFinished() fires
  when playback reaches the last slice.
 ***************************************************************************/

#ifndef SICNU_APP_WIDGETS_TIMELINE_SCRUBBER_WIDGET_H
#define SICNU_APP_WIDGETS_TIMELINE_SCRUBBER_WIDGET_H

#include <QTimer>
#include <QWidget>

#include <vector>

namespace sicnu::gui
{

class TimelineScrubberWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit TimelineScrubberWidget( QWidget *parent = nullptr );
    ~TimelineScrubberWidget() override = default;

    /// Replaces the acquisition timeline (ISO-8601 dates, ascending).
    void setTimelineDates( const std::vector<QString> &isoDates );
    /// Commits the current slice index (clamped); emits dateChanged() when
    /// the index actually changed.
    void setCurrentIndex( int index );
    int currentIndex() const { return mCurrentIndex; }

    /// Playback at @a framesPerSlice ms per slice for 1.0x (D16 §G).
    void play();
    void pause();
    void setPlaySpeed( float speedMultiplier );

  signals:
    void dateChanged( int index, const QString &isoDate );
    void playbackFinished();

  protected:
    void paintEvent( QPaintEvent *event ) override;
    void mousePressEvent( QMouseEvent *event ) override;
    void mouseMoveEvent( QMouseEvent *event ) override;

  private:
    void onTick();
    int indexAtX( int x ) const;
    int tickX( int index ) const;

    std::vector<QString> mDates;
    int mCurrentIndex = -1;
    QTimer mTimer;
    float mPlaySpeed = 1.0f;
    float mFrameAccumulator = 0.0f;
    static constexpr int kFrameMs = 16;             // 60 fps pulse
    static constexpr float kFramesPerSlice = 30.0f; // ~0.5 s per slice at 1.0x
    static constexpr int kSnapPixels = 5;           // snap-to-acquisition
};

} // namespace sicnu::gui

#endif // SICNU_APP_WIDGETS_TIMELINE_SCRUBBER_WIDGET_H
