// help_viewer_dialog.h — Markdown-rendered interactive user guide and help viewer.
#pragma once

#include <QDialog>
#include <QFont>
#include <QString>

class QLineEdit;
class QPushButton;
class QSplitter;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

class HelpViewerDialog : public QDialog
{
  Q_OBJECT

public:
  explicit HelpViewerDialog( QWidget *parent = nullptr );
  ~HelpViewerDialog() override;

  bool loadGuide();
  bool loadDocument( const QString &filePath );
  void loadMarkdownContent( const QString &markdown, const QString &sourcePath = QString() );
  void scrollToSection( const QString &titleOrAnchor );
  void filterToc( const QString &pattern );
  void searchContent( const QString &text, bool forward = true );
  void zoomIn();
  void zoomOut();
  void resetZoom();
  void openInExternalBrowser();

  QTreeWidget *tocTree() const { return m_tocTree; }
  QTextBrowser *textBrowser() const { return m_textBrowser; }
  QLineEdit *filterEdit() const { return m_filterEdit; }
  QLineEdit *searchEdit() const { return m_searchEdit; }
  QPushButton *zoomInButton() const { return m_zoomInBtn; }
  QPushButton *zoomOutButton() const { return m_zoomOutBtn; }
  QPushButton *zoomResetButton() const { return m_zoomResetBtn; }
  QPushButton *externalBrowserButton() const { return m_externalBtn; }
  QString currentFilePath() const { return m_currentFilePath; }
  QString currentMarkdown() const { return m_currentMarkdown; }

  static QString fallbackGuideMarkdown();

private slots:
  void onTocItemClicked( QTreeWidgetItem *item, int column );
  void onAnchorClicked( const QUrl &link );

private:
  void setupUi();
  void buildToc( const QString &markdown );

  QSplitter *m_splitter = nullptr;
  QTreeWidget *m_tocTree = nullptr;
  QTextBrowser *m_textBrowser = nullptr;
  QLineEdit *m_filterEdit = nullptr;
  QLineEdit *m_searchEdit = nullptr;
  QPushButton *m_zoomInBtn = nullptr;
  QPushButton *m_zoomOutBtn = nullptr;
  QPushButton *m_zoomResetBtn = nullptr;
  QPushButton *m_externalBtn = nullptr;

  QFont m_defaultFont;
  QString m_currentFilePath;
  QString m_currentMarkdown;
};
