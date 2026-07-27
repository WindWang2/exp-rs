/***************************************************************************
 * ribbon_controller.h  —  ArcGIS Pro–style RS product ribbon
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QString>

class QWidget;
class QToolButton;
class QHBoxLayout;
class QSlider;
class QComboBox;
class QgisDesktopWindow;
class QgsRasterLayer;

/**
 * ArcGIS Pro–inspired ribbon (no classic menu bar):
 *
 *   ┌ Quick Access Toolbar (new / open / save / settings) ──────────┐
 *   ├ Tabs: 工程 | 编辑 | 矢量编辑 | 地图 | 数据 | … | 任务          │
 *   └ Tab content: groups | large tools | sliders | combos ────────┘
 *
 * Atomic RS tools emit openWorkflowTool(definitionId).
 *
 * 「编辑」= 撤销/剪贴板等通用编辑
 * 「矢量编辑」= 要素数字化 / 几何修改
 */
class RibbonController : public QObject
{
    Q_OBJECT
  public:
    explicit RibbonController( QgisDesktopWindow *window, QObject *parent = nullptr );

    /** Full-width ribbon widget (objectName rsRibbonBar). */
    QWidget *createRibbonBar();

  public slots:
    /** Refresh band-composition combos when the active raster changes. */
    void syncBandCombos();

  signals:
    void openWorkflowTool( const QString &definitionId );

  private:
    struct GroupHost
    {
        QWidget *widget = nullptr;
        QHBoxLayout *toolsLayout = nullptr;
    };

    QWidget *makeTabPage();
    GroupHost addGroup( QHBoxLayout *pageLayout, const QString &title );
    QToolButton *addToolButton( GroupHost &group,
                                const QString &text,
                                const char *iconAlias,
                                const QString &tooltip = QString(),
                                bool large = true );
    QSlider *addSlider( GroupHost &group,
                        const QString &title,
                        int minVal,
                        int maxVal,
                        int value,
                        const QString &tooltip,
                        const QString &suffix = QString() );
    /**
     * Compact titled combobox for ribbon groups (band pickers, modes, etc.).
     * Returns the QComboBox; caller fills items and connects signals.
     */
    QComboBox *addComboBox( GroupHost &group,
                            const QString &title,
                            const QString &tooltip = QString(),
                            int minWidth = 88 );
    void addGroupSeparator( QHBoxLayout *pageLayout );
    QWidget *makeQuickAccessToolbar( QWidget *parent );
    QgsRasterLayer *currentRasterLayer() const;

    void fillBandItems( QComboBox *combo, int bandCount, int selectedBand );
    void applyBandCompositionFromCombos();
    void applyRenderModeFromCombo();
    void wireBandComboSignals();
    /** Right-click on ribbon chrome → panels/toolbars toggle menu (QGIS-style). */
    void installChromeContextMenu( QWidget *widget );

    QgisDesktopWindow *m_window = nullptr;

    // 地图 → 波段合成
    QComboBox *m_renderModeCombo = nullptr;
    QComboBox *m_redBandCombo = nullptr;
    QComboBox *m_greenBandCombo = nullptr;
    QComboBox *m_blueBandCombo = nullptr;
    QComboBox *m_grayBandCombo = nullptr;
    bool m_bandComboUpdating = false;
};
