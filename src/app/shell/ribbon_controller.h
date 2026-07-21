/***************************************************************************
 * ribbon_controller.h  —  six-tab RS workflow ribbon above the map canvas
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QString>

class QWidget;
class QToolButton;
class QHBoxLayout;
class QgisDesktopWindow;

/**
 * Builds the main-shell ribbon (工程 / 数据 / 预处理 / 分析 / 分类/解译 / 制图).
 *
 * Project/data/workspace buttons call QgisDesktopWindow slots directly.
 * Atomic RS tools emit openWorkflowTool(definitionId) for the session controller.
 */
class RibbonController : public QObject
{
    Q_OBJECT
  public:
    explicit RibbonController( QgisDesktopWindow *window, QObject *parent = nullptr );

    /** Build ribbon QWidget (objectName rsRibbonBar) to place above the canvas. */
    QWidget *createRibbonBar();

  signals:
    void openWorkflowTool( const QString &definitionId );

  private:
    QWidget *makeTabPage();
    QToolButton *addToolButton( QHBoxLayout *layout,
                                const QString &text,
                                const char *iconAlias,
                                const QString &tooltip = QString() );

    QgisDesktopWindow *m_window = nullptr;
};
