// src/app/widgets/comparison_widget.h
#pragma once

#include <QWidget>
#include <QPixmap>
#include <QTimer>

class QSlider;
class QPushButton;
class QComboBox;
class QLabel;

/**
 * Widget for comparing two raster layers side-by-side or with flicker mode.
 * Supports:
 * - Split-screen: vertical slider divides left/right images
 * - Flicker mode: alternates between two images at configurable speed
 */
class ComparisonWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ComparisonWidget(QWidget *parent = nullptr);

    void setLeftImage(const QPixmap &pixmap);
    void setRightImage(const QPixmap &pixmap);

    /// True once a left image has been set (used by tests / UI enablement).
    bool hasLeftImage() const { return !m_leftImage.isNull(); }
    /// True once a right image has been set.
    bool hasRightImage() const { return !m_rightImage.isNull(); }

    enum class ComparisonMode {
        SplitScreen,
        Flicker
    };

    void setMode(ComparisonMode mode);
    ComparisonMode mode() const { return m_mode; }

    void setFlickerInterval(int ms);
    int flickerInterval() const { return m_flickerInterval; }

signals:
    void modeChanged(ComparisonMode mode);
    void flickerIntervalChanged(int ms);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void onFlickerTimeout();
    void onModeComboChanged(int index);
    void onFlickerButtonToggled(bool checked);

private:
    void setupUi();
    void updateFlicker();

    QPixmap m_leftImage;
    QPixmap m_rightImage;
    QPixmap m_scaledLeft;
    QPixmap m_scaledRight;

    ComparisonMode m_mode = ComparisonMode::SplitScreen;
    int m_splitPosition = 50; // percentage (0-100)
    int m_flickerInterval = 500; // ms
    bool m_showLeft = true; // for flicker mode

    QSlider *m_splitSlider = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QPushButton *m_flickerButton = nullptr;
    QLabel *m_infoLabel = nullptr;
    QTimer m_flickerTimer;
};
