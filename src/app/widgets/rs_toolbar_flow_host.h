#pragma once

#include <QHash>
#include <QList>
#include <QWidget>

class QToolBar;
class QLabel;

/**
 * Adaptive 1–2 row host under the Ribbon for product toolbars.
 *
 * - Each toolbar can be dragged (reorder / place on row 1 or 2).
 * - Each toolbar width is resizable (right grip) so more icons appear.
 * - Flow is left-to-right; wraps to a second row when needed (max 2 rows).
 * - Host height collapses to one row when everything fits on a single line.
 */
class RsToolbarFlowHost : public QWidget
{
    Q_OBJECT
  public:
    static constexpr int kRowH = 32;
    static constexpr int kMaxRows = 2;
    static constexpr int kMinBarW = 96;
    static constexpr int kDefaultBarW = 280;
    static constexpr int kGripW = 8;
    static constexpr int kResizeW = 8;
    static constexpr int kSpacing = 4;
    static constexpr int kMargin = 4;

    explicit RsToolbarFlowHost( QWidget *parent = nullptr );

    /** Register product toolbars (order = default left-to-right order). */
    void setProductToolbars( const QList<QToolBar *> &bars );

    bool hasProductToolbars() const { return !m_chips.isEmpty(); }

    /**
     * Apply visibility map (true = show). Reparents toolbars into chips and reflows.
     * Does not touch QAction toggles — caller blocks those.
     */
    void applyVisibility( const QHash<QToolBar *, bool> &wantByBar );

    int usedHeight() const { return m_usedRows * kRowH; }
    int usedRows() const { return m_usedRows; }

  signals:
    /** Host height or content geometry changed — chrome should resize. */
    void geometryChanged();

  protected:
    void resizeEvent( QResizeEvent *event ) override;
    void paintEvent( QPaintEvent *event ) override;
    bool eventFilter( QObject *watched, QEvent *event ) override;

  private:
    struct Chip
    {
        QToolBar *tb = nullptr;
        QWidget *frame = nullptr;
        QWidget *dragGrip = nullptr;
        QWidget *resizeGrip = nullptr;
        int width = kDefaultBarW;
        int order = 0;
        bool visible = false;
    };

    void ensureChips();
    void reflow();
    void loadSettings();
    void saveSettings() const;
    Chip *chipFor( QToolBar *tb );
    Chip *chipForFrame( QWidget *frame );
    QList<Chip *> visibleChipsSorted();
    int insertIndexAt( const QPoint &posInHost ) const;

    QList<Chip> m_chips;
    int m_usedRows = 0;

    // Drag state
    Chip *m_dragChip = nullptr;
    QPoint m_dragOffset;
    bool m_dragging = false;

    // Resize state
    Chip *m_resizeChip = nullptr;
    int m_resizeStartW = 0;
    int m_resizeStartX = 0;
    bool m_resizing = false;
};
