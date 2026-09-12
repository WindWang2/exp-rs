// help_viewer_dialog.cpp — Markdown-rendered interactive user guide and help viewer.
#include "help_viewer_dialog.h"
#include "app_paths.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "dialog_utils.h"

#include <functional>

HelpViewerDialog::HelpViewerDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "RS Studio User Manual and Help" ) );
  SicnuUi::polishDialog( this, 720 );
  resize( 1060, 720 );
  setMinimumSize( 680, 460 );

  setupUi();
  loadGuide();
}

HelpViewerDialog::~HelpViewerDialog() = default;

void HelpViewerDialog::setupUi()
{
  auto *rootLayout = SicnuUi::makeDialogRootLayout( this );

  m_splitter = new QSplitter( Qt::Horizontal, this );

  // --- Left Pane: TOC with Filter ---
  auto *leftWidget = new QWidget( m_splitter );
  auto *leftLayout = new QVBoxLayout( leftWidget );
  leftLayout->setContentsMargins( 0, 0, 0, 0 );
  leftLayout->setSpacing( 6 );

  auto *tocHeaderLayout = new QHBoxLayout();
  auto *tocTitle = new QLabel( tr( "<b>Documentation</b>" ), leftWidget );
  tocHeaderLayout->addWidget( tocTitle );
  leftLayout->addLayout( tocHeaderLayout );

  m_filterEdit = new QLineEdit( leftWidget );
  m_filterEdit->setObjectName( QStringLiteral( "helpViewerFilterEdit" ) );
  m_filterEdit->setPlaceholderText( tr( "Filter the section catalog..." ) );
  m_filterEdit->setClearButtonEnabled( true );
  connect( m_filterEdit, &QLineEdit::textChanged, this, &HelpViewerDialog::filterToc );
  leftLayout->addWidget( m_filterEdit );

  m_tocTree = new QTreeWidget( leftWidget );
  m_tocTree->setObjectName( QStringLiteral( "helpViewerTocTree" ) );
  m_tocTree->setHeaderHidden( true );
  m_tocTree->setAnimated( true );
  m_tocTree->setAlternatingRowColors( true );
  connect( m_tocTree, &QTreeWidget::itemClicked, this, &HelpViewerDialog::onTocItemClicked );
  leftLayout->addWidget( m_tocTree, 1 );

  m_splitter->addWidget( leftWidget );

  // --- Right Pane: Document Viewer with Search & Zoom Toolbar ---
  auto *rightWidget = new QWidget( m_splitter );
  auto *rightLayout = new QVBoxLayout( rightWidget );
  rightLayout->setContentsMargins( 0, 0, 0, 0 );
  rightLayout->setSpacing( 6 );

  auto *topBarLayout = new QHBoxLayout();
  topBarLayout->setSpacing( 6 );

  m_searchEdit = new QLineEdit( rightWidget );
  m_searchEdit->setObjectName( QStringLiteral( "helpViewerSearchEdit" ) );
  m_searchEdit->setPlaceholderText( tr( "Find in Document..." ) );
  m_searchEdit->setClearButtonEnabled( true );
  connect( m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
    searchContent( m_searchEdit->text(), true );
  } );
  topBarLayout->addWidget( m_searchEdit, 1 );

  auto *findNextBtn = new QPushButton( tr( "Next" ), rightWidget );
  SicnuUi::markSecondary( findNextBtn );
  connect( findNextBtn, &QPushButton::clicked, this, [this]() {
    searchContent( m_searchEdit->text(), true );
  } );
  topBarLayout->addWidget( findNextBtn );

  auto *findPrevBtn = new QPushButton( tr( "Previous" ), rightWidget );
  SicnuUi::markSecondary( findPrevBtn );
  connect( findPrevBtn, &QPushButton::clicked, this, [this]() {
    searchContent( m_searchEdit->text(), false );
  } );
  topBarLayout->addWidget( findPrevBtn );

  topBarLayout->addSpacing( 8 );

  m_zoomInBtn = new QPushButton( tr( "Zoom In" ), rightWidget );
  SicnuUi::markSecondary( m_zoomInBtn );
  connect( m_zoomInBtn, &QPushButton::clicked, this, &HelpViewerDialog::zoomIn );
  topBarLayout->addWidget( m_zoomInBtn );

  m_zoomOutBtn = new QPushButton( tr( "Zoom Out" ), rightWidget );
  SicnuUi::markSecondary( m_zoomOutBtn );
  connect( m_zoomOutBtn, &QPushButton::clicked, this, &HelpViewerDialog::zoomOut );
  topBarLayout->addWidget( m_zoomOutBtn );

  m_zoomResetBtn = new QPushButton( tr( "100%" ), rightWidget );
  SicnuUi::markSecondary( m_zoomResetBtn );
  connect( m_zoomResetBtn, &QPushButton::clicked, this, &HelpViewerDialog::resetZoom );
  topBarLayout->addWidget( m_zoomResetBtn );

  topBarLayout->addSpacing( 8 );

  m_externalBtn = new QPushButton( tr( "External Browser" ), rightWidget );
  SicnuUi::markSecondary( m_externalBtn );
  connect( m_externalBtn, &QPushButton::clicked, this, &HelpViewerDialog::openInExternalBrowser );
  topBarLayout->addWidget( m_externalBtn );

  rightLayout->addLayout( topBarLayout );

  m_textBrowser = new QTextBrowser( rightWidget );
  m_textBrowser->setObjectName( QStringLiteral( "helpViewerTextBrowser" ) );
  m_textBrowser->setOpenExternalLinks( false );
  m_textBrowser->setOpenLinks( false );
  m_defaultFont = m_textBrowser->font();
  connect( m_textBrowser, &QTextBrowser::anchorClicked, this, &HelpViewerDialog::onAnchorClicked );
  rightLayout->addWidget( m_textBrowser, 1 );

  m_splitter->addWidget( rightWidget );
  m_splitter->setSizes( { 280, 780 } );

  rootLayout->addWidget( m_splitter, 1 );

  auto *bottomBox = new QDialogButtonBox( QDialogButtonBox::Close, this );
  bottomBox->button( QDialogButtonBox::Close )->setText( tr( "Close" ) );
  SicnuUi::markSecondary( bottomBox->button( QDialogButtonBox::Close ) );
  connect( bottomBox, &QDialogButtonBox::rejected, this, &QDialog::accept );
  rootLayout->addWidget( bottomBox );
}

bool HelpViewerDialog::loadGuide()
{
  const QString resolved = AppPaths::resolveDataPath( QStringLiteral( "docs/USER_GUIDE.md" ) );
  const QStringList candidates = {
    resolved,
    QDir( QCoreApplication::applicationDirPath() ).absoluteFilePath( QStringLiteral( "../docs/USER_GUIDE.md" ) ),
    QDir( QCoreApplication::applicationDirPath() ).absoluteFilePath( QStringLiteral( "docs/USER_GUIDE.md" ) ),
    QDir::current().absoluteFilePath( QStringLiteral( "docs/USER_GUIDE.md" ) )
  };

  for ( const QString &candidate : candidates )
  {
    if ( !candidate.isEmpty() && QFile::exists( candidate ) )
    {
      return loadDocument( candidate );
    }
  }

  loadMarkdownContent( fallbackGuideMarkdown(), QStringLiteral( "docs/USER_GUIDE.md" ) );
  return true;
}

bool HelpViewerDialog::loadDocument( const QString &filePath )
{
  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    loadMarkdownContent( fallbackGuideMarkdown(), filePath );
    return false;
  }

  const QString content = QString::fromUtf8( file.readAll() );
  file.close();

  loadMarkdownContent( content, filePath );
  return true;
}

void HelpViewerDialog::loadMarkdownContent( const QString &markdown, const QString &sourcePath )
{
  m_currentMarkdown = markdown;
  m_currentFilePath = sourcePath;

  m_textBrowser->setMarkdown( markdown );
  m_textBrowser->moveCursor( QTextCursor::Start );

  buildToc( markdown );
}

void HelpViewerDialog::buildToc( const QString &markdown )
{
  m_tocTree->clear();
  const QStringList lines = markdown.split( QLatin1Char( '\n' ) );

  QTreeWidgetItem *lastH1 = nullptr;
  QTreeWidgetItem *lastH2 = nullptr;
  bool inCodeBlock = false;

  for ( int i = 0; i < lines.size(); ++i )
  {
    const QString line = lines[i].trimmed();
    if ( line.startsWith( QLatin1String( "```" ) ) )
    {
      inCodeBlock = !inCodeBlock;
      continue;
    }
    if ( inCodeBlock )
      continue;

    if ( line.startsWith( QLatin1String( "# " ) ) )
    {
      const QString title = line.mid( 2 ).trimmed();
      auto *item = new QTreeWidgetItem( m_tocTree );
      item->setText( 0, title );
      item->setData( 0, Qt::UserRole, title );
      lastH1 = item;
      lastH2 = nullptr;
    }
    else if ( line.startsWith( QLatin1String( "## " ) ) )
    {
      const QString title = line.mid( 3 ).trimmed();
      QTreeWidgetItem *parent = lastH1 ? lastH1 : new QTreeWidgetItem( m_tocTree );
      auto *item = new QTreeWidgetItem( parent );
      item->setText( 0, title );
      item->setData( 0, Qt::UserRole, title );
      lastH2 = item;
    }
    else if ( line.startsWith( QLatin1String( "### " ) ) )
    {
      const QString title = line.mid( 4 ).trimmed();
      QTreeWidgetItem *parent = lastH2 ? lastH2 : ( lastH1 ? lastH1 : new QTreeWidgetItem( m_tocTree ) );
      auto *item = new QTreeWidgetItem( parent );
      item->setText( 0, title );
      item->setData( 0, Qt::UserRole, title );
    }
  }

  m_tocTree->expandAll();
}

void HelpViewerDialog::onTocItemClicked( QTreeWidgetItem *item, int )
{
  if ( !item )
    return;
  const QString title = item->data( 0, Qt::UserRole ).toString();
  if ( !title.isEmpty() )
  {
    scrollToSection( title );
  }
}

void HelpViewerDialog::onAnchorClicked( const QUrl &link )
{
  const QString frag = link.fragment();
  if ( !frag.isEmpty() )
  {
    m_textBrowser->scrollToAnchor( frag );
  }
  else if ( link.scheme() == QLatin1String( "http" ) || link.scheme() == QLatin1String( "https" ) )
  {
    QDesktopServices::openUrl( link );
  }
  else
  {
    scrollToSection( link.toString().remove( QLatin1Char( '#' ) ) );
  }
}

void HelpViewerDialog::scrollToSection( const QString &titleOrAnchor )
{
  if ( titleOrAnchor.isEmpty() )
    return;

  m_textBrowser->scrollToAnchor( titleOrAnchor );

  QTextDocument *doc = m_textBrowser->document();
  if ( !doc )
    return;

  QTextCursor cursor = doc->find( titleOrAnchor );
  if ( !cursor.isNull() )
  {
    m_textBrowser->setTextCursor( cursor );
    m_textBrowser->ensureCursorVisible();
  }
}

void HelpViewerDialog::filterToc( const QString &pattern )
{
  const QString term = pattern.trimmed();

  std::function<bool( QTreeWidgetItem * )> filterItem = [&]( QTreeWidgetItem *item ) -> bool {
    bool match = term.isEmpty() || item->text( 0 ).contains( term, Qt::CaseInsensitive );
    bool anyChildMatch = false;

    for ( int i = 0; i < item->childCount(); ++i )
    {
      if ( filterItem( item->child( i ) ) )
      {
        anyChildMatch = true;
      }
    }

    const bool visible = match || anyChildMatch;
    item->setHidden( !visible );
    if ( visible && !term.isEmpty() )
    {
      item->setExpanded( true );
    }
    return visible;
  };

  for ( int i = 0; i < m_tocTree->topLevelItemCount(); ++i )
  {
    filterItem( m_tocTree->topLevelItem( i ) );
  }
}

void HelpViewerDialog::searchContent( const QString &text, bool forward )
{
  if ( text.isEmpty() )
    return;

  QTextDocument::FindFlags flags;
  if ( !forward )
    flags |= QTextDocument::FindBackward;

  bool found = m_textBrowser->find( text, flags );
  if ( !found )
  {
    QTextCursor cur = m_textBrowser->textCursor();
    if ( forward )
      cur.movePosition( QTextCursor::Start );
    else
      cur.movePosition( QTextCursor::End );
    m_textBrowser->setTextCursor( cur );
    m_textBrowser->find( text, flags );
  }
}

void HelpViewerDialog::zoomIn()
{
  m_textBrowser->zoomIn( 1 );
}

void HelpViewerDialog::zoomOut()
{
  m_textBrowser->zoomOut( 1 );
}

void HelpViewerDialog::resetZoom()
{
  m_textBrowser->setFont( m_defaultFont );
}

void HelpViewerDialog::openInExternalBrowser()
{
  if ( !m_currentFilePath.isEmpty() && QFile::exists( m_currentFilePath ) )
  {
    QDesktopServices::openUrl( QUrl::fromLocalFile( m_currentFilePath ) );
  }
  else
  {
    const QString resolved = AppPaths::resolveDataPath( QStringLiteral( "docs/USER_GUIDE.md" ) );
    if ( QFile::exists( resolved ) )
      QDesktopServices::openUrl( QUrl::fromLocalFile( resolved ) );
  }
}

QString HelpViewerDialog::fallbackGuideMarkdown()
{
  return QStringLiteral(
    tr("# RS Studio (exp-rs) Comprehensive User Manual and Operation Guide\n\n")
    tr("> **Version**: v2.0 Professional  \n")
    tr("> **System document code**: DOC-RS-STUDIO-USERGUIDE-CN  \n\n")
    tr("# Chapter 1: System Overview and Quick Start\n")
    tr("RS Studio is a new-generation desktop intelligent geospatial analysis platform for modern remote-sensing research, university teaching and industrial production.\n\n")
    tr("# Chapter 2: Loading and Managing Remote-Sensing Data\n")
    tr("Provides automatic multi-source satellite product import, STAC cloud search and Data Manager asset management.\n\n")
    tr("# Chapter 3: Viewport Visualization and Multi-Source Linkage\n")
    tr("Provides linked split viewports, swipe comparison, band composition and real-time display stretching.\n\n")
    tr("# Chapter 4: The Full Pixel-Level Classification Workflow\n")
    tr("Provides a complete 7-step guided workflow: class scheme, ROI collection, JM-distance separability evaluation, model training, confusion-matrix accuracy assessment, post-classification and result export.\n\n")
    tr("# Chapter 5: Object-Based Image Analysis (OBIA)\n")
    tr("Provides multiresolution segmentation, hierarchical topology trees, GLCM texture and geometric feature extraction, and object classification.\n\n")
    tr("# Chapter 6: Spectral Analysis and Hyperspectral Tools\n")
    tr("Provides spectral profiles, continuum removal, library SAM / SID matching, linear unmixing and the RX anomaly detector.\n\n")
    tr("# Chapter 7: Remote-Sensing Preprocessing and Image Enhancement\n")
    tr("Covers radiometric calibration, atmospheric correction (DOS1, DOS2, QUAC), cloud/snow QA masking, image registration and spatial filtering.\n\n")
    tr("# Chapter 8: AI Copilot Assistant\n")
    tr("LLM-powered natural-language remote-sensing analysis chat, tool calls and automated DAG pipeline orchestration.\n\n")
    tr("# Chapter 9: Troubleshooting and Diagnostics\n")
    tr("Covers startup dependencies, projection anomalies, out-of-memory tiling optimization and network connectivity troubleshooting.\n\n")
    tr("# Chapter 10: Shortcut and Operation Quick Reference\n")
    tr("A quick reference of shortcuts for projects, viewport navigation, vector editing and image registration.\n")
  );
}
