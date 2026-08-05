// src/app/widgets/comparison_widget.cpp
#include "comparison_widget.h"

#include <QPainter>
#include <QSlider>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QResizeEvent>

ComparisonWidget::ComparisonWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    connect(&m_flickerTimer, &QTimer::timeout, this, &ComparisonWidget::onFlickerTimeout);
}

void ComparisonWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Controls bar
    auto *controlsLayout = new QHBoxLayout();
    controlsLayout->setContentsMargins(4, 4, 4, 4);

    // Mode selector
    controlsLayout->addWidget(new QLabel(tr("Mode:"), this));
    m_modeCombo = new QComboBox(this);
    m_modeCombo->setToolTip(tr("对比模式：分屏（左右滑动对比）或闪烁（自动切换）。"));
    m_modeCombo->addItems({tr("Split Screen"), tr("Flicker")});
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ComparisonWidget::onModeComboChanged);
    controlsLayout->addWidget(m_modeCombo);

    controlsLayout->addStretch();

    // Split slider (for split-screen mode)
    controlsLayout->addWidget(new QLabel(tr("Split:"), this));
    m_splitSlider = new QSlider(Qt::Horizontal, this);
    m_splitSlider->setToolTip(tr("分屏位置（0=全左，100=全右）。"));
    m_splitSlider->setRange(0, 100);
    m_splitSlider->setValue(50);
    m_splitSlider->setTickPosition(QSlider::TicksBelow);
    m_splitSlider->setTickInterval(10);
    connect(m_splitSlider, &QSlider::valueChanged, this, [this](int value) {
        m_splitPosition = value;
        update();
    });
    controlsLayout->addWidget(m_splitSlider);

    // Flicker button (for flicker mode)
    m_flickerButton = new QPushButton(tr("Start Flicker"), this);
    m_flickerButton->setToolTip(tr("开始/停止闪烁对比（自动在两个图层间切换）。"));
    m_flickerButton->setCheckable(true);
    m_flickerButton->setVisible(false);
    connect(m_flickerButton, &QPushButton::toggled, this, &ComparisonWidget::onFlickerButtonToggled);
    controlsLayout->addWidget(m_flickerButton);

    // Info label
    m_infoLabel = new QLabel(this);
    m_infoLabel->setAlignment(Qt::AlignCenter);
    controlsLayout->addWidget(m_infoLabel);

    mainLayout->addLayout(controlsLayout);

    // Set minimum size for the painting area
    setMinimumSize(400, 300);
}

void ComparisonWidget::setLeftImage(const QPixmap &pixmap)
{
    m_leftImage = pixmap;
    m_scaledLeft = m_leftImage.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    update();
}

void ComparisonWidget::setRightImage(const QPixmap &pixmap)
{
    m_rightImage = pixmap;
    m_scaledRight = m_rightImage.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    update();
}

void ComparisonWidget::setMode(ComparisonMode mode)
{
    if (m_mode == mode) return;

    m_mode = mode;
    m_modeCombo->setCurrentIndex(static_cast<int>(mode));

    // Show/hide controls based on mode
    m_splitSlider->setVisible(mode == ComparisonMode::SplitScreen);
    m_flickerButton->setVisible(mode == ComparisonMode::Flicker);

    // Stop flicker if switching away
    if (mode != ComparisonMode::Flicker) {
        m_flickerTimer.stop();
        m_flickerButton->setChecked(false);
    }

    update();
    emit modeChanged(mode);
}

void ComparisonWidget::setFlickerInterval(int ms)
{
    m_flickerInterval = ms;
    if (m_flickerTimer.isActive()) {
        m_flickerTimer.setInterval(ms);
    }
    emit flickerIntervalChanged(ms);
}

void ComparisonWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);

    // Fill background
    painter.fillRect(rect(), Qt::darkGray);

    if (m_scaledLeft.isNull() && m_scaledRight.isNull()) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("Load two layers to compare"));
        return;
    }

    // Calculate image position (centered)
    int x = (width() - m_scaledLeft.width()) / 2;
    int y = (height() - m_scaledLeft.height()) / 2;

    if (m_mode == ComparisonMode::SplitScreen) {
        // Draw left image (clipped to left of split)
        int splitX = width() * m_splitPosition / 100;

        if (!m_scaledLeft.isNull()) {
            painter.save();
            painter.setClipRect(0, 0, splitX, height());
            painter.drawPixmap(x, y, m_scaledLeft);
            painter.restore();
        }

        // Draw right image (clipped to right of split)
        if (!m_scaledRight.isNull()) {
            painter.save();
            painter.setClipRect(splitX, 0, width() - splitX, height());
            painter.drawPixmap(x, y, m_scaledRight);
            painter.restore();
        }

        // Draw split line
        painter.setPen(QPen(Qt::white, 2));
        painter.drawLine(splitX, 0, splitX, height());

        // Draw labels
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 10, QFont::Bold));
        if (!m_scaledLeft.isNull()) {
            painter.drawText(10, 20, tr("Left"));
        }
        if (!m_scaledRight.isNull()) {
            painter.drawText(splitX + 10, 20, tr("Right"));
        }
    } else {
        // Flicker mode - show one image at a time
        const QPixmap &current = m_showLeft ? m_scaledLeft : m_scaledRight;
        if (!current.isNull()) {
            painter.drawPixmap(x, y, current);
        }

        // Draw indicator
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 10, QFont::Bold));
        painter.drawText(10, 20, m_showLeft ? tr("Layer A") : tr("Layer B"));
    }
}

void ComparisonWidget::resizeEvent(QResizeEvent * /*event*/)
{
    if (!m_leftImage.isNull()) {
        m_scaledLeft = m_leftImage.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (!m_rightImage.isNull()) {
        m_scaledRight = m_rightImage.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
}

void ComparisonWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_mode == ComparisonMode::SplitScreen && event->button() == Qt::LeftButton) {
        m_splitPosition = event->position().x() * 100 / width();
        m_splitSlider->setValue(m_splitPosition);
        update();
    }
}

void ComparisonWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_mode == ComparisonMode::SplitScreen && event->buttons() & Qt::LeftButton) {
        m_splitPosition = event->position().x() * 100 / width();
        m_splitSlider->setValue(m_splitPosition);
        update();
    }
}

void ComparisonWidget::onFlickerTimeout()
{
    m_showLeft = !m_showLeft;
    update();
}

void ComparisonWidget::onModeComboChanged(int index)
{
    setMode(static_cast<ComparisonMode>(index));
}

void ComparisonWidget::onFlickerButtonToggled(bool checked)
{
    if (checked) {
        m_flickerTimer.start(m_flickerInterval);
        m_flickerButton->setText(tr("Stop Flicker"));
    } else {
        m_flickerTimer.stop();
        m_flickerButton->setText(tr("Start Flicker"));
        m_showLeft = true;
        update();
    }
}
