/***************************************************************************
 * ribbon_controller.h  —  compact top-chrome RS workflow ribbon
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QString>

class QWidget;
class QToolButton;
class QHBoxLayout;
class QStackedWidget;
class QButtonGroup;
class QgisDesktopWindow;

/**
 * Builds a compact product ribbon (menu-bar height band, not canvas overlay):
 * 工程 / 数据 / 预处理 / 分析 / 分类·解译 / 制图 / 视图
 *
 * Atomic RS tools emit openWorkflowTool(definitionId).
 */
class RibbonController : public QObject
{
    Q_OBJECT
  public:
    explicit RibbonController( QgisDesktopWindow *window, QObject *parent = nullptr );

    /**
     * Build ribbon widget (objectName rsRibbonBar).
     * Place under the menu bar via a non-movable QToolBar host — not in central canvas.
     */
    QWidget *createRibbonBar();

  signals:
    void openWorkflowTool( const QString &definitionId );

  private:
    QWidget *makeToolStrip();
    QToolButton *addToolButton( QHBoxLayout *layout,
                                const QString &text,
                                const char *iconAlias,
                                const QString &tooltip = QString() );
    void addGroupSeparator( QHBoxLayout *layout );

    QgisDesktopWindow *m_window = nullptr;
};
