/***************************************************************************
 * help_center_dialog.h — in-app Help Center
 *
 * Local, offline, searchable rendering of the help knowledge base:
 *   left  — search box + category tree (goal-defined IA) + search results
 *   right — structured HTML rendering of the selected descriptor
 *
 * Topic anchors use helpid://<id> links (related topics, diagnostics),
 * resolved internally — no network, no filesystem navigation. Search runs
 * over the prebuilt HelpSearchIndex (deterministic, <5 ms in release).
 ***************************************************************************/
#pragma once

#include "help/help_registry.h"
#include "help/help_search_index.h"

#include <QDialog>
#include <QString>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;
class QLabel;

namespace sicnu::app
{

class HelpCenterDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit HelpCenterDialog( QWidget *parent = nullptr );

    /// Navigates to @p helpId (search results update to reflect the topic).
    void navigateTo( const QString &helpId );

    /// Standard window title.
    static QString dialogTitle() { return tr( "Help Center" ); }

  protected:
    void keyPressEvent( QKeyEvent *event ) override;

  private:
    void buildCategories();
    void showTopic( const QString &helpId );
    void runSearch( const QString &query );
    void renderHome();
    QString renderTopicHtml( const sicnu::help::HelpDescriptor &descriptor ) const;
    QString escapeHtml( const QString &text ) const;
    QTreeWidgetItem *categoryItem( const QString &category );

    sicnu::help::HelpSearchIndex m_index;
    QLineEdit *m_searchBox = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTextBrowser *m_browser = nullptr;
    QLabel *m_countLabel = nullptr;
    QHash<QString, QTreeWidgetItem *> m_categoryItems;
};

} // namespace sicnu::app
