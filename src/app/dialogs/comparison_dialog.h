// src/app/dialogs/comparison_dialog.h
#pragma once

#include <QDialog>
#include <QLabel>

class ComparisonWidget;
class QComboBox;
class QPushButton;
class RasterLayerCombo;
class QgsRasterLayer;
class QgsMapRendererParallelJob;

/**
 * Dialog for comparing two raster layers side-by-side or with flicker mode.
 */
class ComparisonDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ComparisonDialog(QWidget *parent = nullptr);

    void setLeftLayer(QgsRasterLayer *layer);
    void setRightLayer(QgsRasterLayer *layer);

private slots:
    void onBrowseLeft();
    void onBrowseRight();
    void onLoadLayers();

private:
    void setupUi();
    void loadLayerToWidget(QgsRasterLayer *layer, bool isLeft);

    ComparisonWidget *m_comparisonWidget = nullptr;
    RasterLayerCombo *m_leftLayerCombo = nullptr;
    RasterLayerCombo *m_rightLayerCombo = nullptr;
    QPushButton *m_loadButton = nullptr;

    QgsRasterLayer *m_leftLayer = nullptr;
    QgsRasterLayer *m_rightLayer = nullptr;

    // Asynchronous preview renders (#634): waitForFinished() on the GUI
    // thread blocked until both 400x400 renders completed - on /vsicurl/
    // or overview-less sources that froze the dialog. One job per side,
    // result delivered via the finished signal (SwipeMapTool pattern).
    QgsMapRendererParallelJob *m_leftJob = nullptr;
    QgsMapRendererParallelJob *m_rightJob = nullptr;
    QLabel *m_leftPreview = nullptr;
    QLabel *m_rightPreview = nullptr;

    void startPreviewRender(QgsRasterLayer *layer, bool isLeft);
};
