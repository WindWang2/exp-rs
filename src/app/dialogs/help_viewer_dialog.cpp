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
  setWindowTitle( tr( "RS Studio 用户手册与帮助文档" ) );
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
  auto *tocTitle = new QLabel( tr( "<b>文档目录</b>" ), leftWidget );
  tocHeaderLayout->addWidget( tocTitle );
  leftLayout->addLayout( tocHeaderLayout );

  m_filterEdit = new QLineEdit( leftWidget );
  m_filterEdit->setObjectName( QStringLiteral( "helpViewerFilterEdit" ) );
  m_filterEdit->setPlaceholderText( tr( "过滤章节目录…" ) );
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
  m_searchEdit->setPlaceholderText( tr( "在文档正文中查找…" ) );
  m_searchEdit->setClearButtonEnabled( true );
  connect( m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
    searchContent( m_searchEdit->text(), true );
  } );
  topBarLayout->addWidget( m_searchEdit, 1 );

  auto *findNextBtn = new QPushButton( tr( "下一个" ), rightWidget );
  SicnuUi::markSecondary( findNextBtn );
  connect( findNextBtn, &QPushButton::clicked, this, [this]() {
    searchContent( m_searchEdit->text(), true );
  } );
  topBarLayout->addWidget( findNextBtn );

  auto *findPrevBtn = new QPushButton( tr( "上一个" ), rightWidget );
  SicnuUi::markSecondary( findPrevBtn );
  connect( findPrevBtn, &QPushButton::clicked, this, [this]() {
    searchContent( m_searchEdit->text(), false );
  } );
  topBarLayout->addWidget( findPrevBtn );

  topBarLayout->addSpacing( 8 );

  m_zoomInBtn = new QPushButton( tr( "放大" ), rightWidget );
  SicnuUi::markSecondary( m_zoomInBtn );
  connect( m_zoomInBtn, &QPushButton::clicked, this, &HelpViewerDialog::zoomIn );
  topBarLayout->addWidget( m_zoomInBtn );

  m_zoomOutBtn = new QPushButton( tr( "缩小" ), rightWidget );
  SicnuUi::markSecondary( m_zoomOutBtn );
  connect( m_zoomOutBtn, &QPushButton::clicked, this, &HelpViewerDialog::zoomOut );
  topBarLayout->addWidget( m_zoomOutBtn );

  m_zoomResetBtn = new QPushButton( tr( "100%" ), rightWidget );
  SicnuUi::markSecondary( m_zoomResetBtn );
  connect( m_zoomResetBtn, &QPushButton::clicked, this, &HelpViewerDialog::resetZoom );
  topBarLayout->addWidget( m_zoomResetBtn );

  topBarLayout->addSpacing( 8 );

  m_externalBtn = new QPushButton( tr( "外部浏览器" ), rightWidget );
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
  bottomBox->button( QDialogButtonBox::Close )->setText( tr( "关闭" ) );
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
    "# RS Studio (exp-rs) 综合用户手册与操作指南\n\n"
    "> **版本**：v2.0 Professional  \n"
    "> **系统文档代码**：DOC-RS-STUDIO-USERGUIDE-CN  \n\n"
    "# 第 1 章：系统概述与快速入门\n"
    "RS Studio 是面向现代遥感科研、高校教学与工业生产的新一代桌面智能地理空间分析平台。\n\n"
    "# 第 2 章：遥感数据加载与管理\n"
    "支持多源卫星产品自动识别导入、STAC 云端检索与 Data Manager 资产管理。\n\n"
    "# 第 3 章：视口可视化与多源联动\n"
    "提供双视口分屏同步联动、卷帘对比 (Swipe)、波段合成与实时显示拉伸。\n\n"
    "# 第 4 章：像素级遥感分类全流程\n"
    "提供完整的 7 步引导流程：类别体系、ROI 采集、JM 距离可分性评价、模型训练、混淆矩阵精度评定、分类后处理与成果导出。\n\n"
    "# 第 5 章：面向对象影像分析 (OBIA)\n"
    "提供多尺度分割、多层级拓扑树、GLCM 纹理与几何特征提取及对象分类。\n\n"
    "# 第 6 章：波谱分析与高光谱工具\n"
    "提供光谱剖面图、连续统去除 (Continuum Removal)、光谱库 SAM / SID 匹配、线性解混与 RX 异常探测。\n\n"
    "# 第 7 章：遥感预处理与图像增强\n"
    "包括辐射定标、大气校正 (DOS1, DOS2, QUAC)、云雪 QA 掩膜、影像配准与空间滤波。\n\n"
    "# 第 8 章：AI Copilot 智能助手\n"
    "基于大语言模型的自然语言遥感分析对话、工具调用与 DAG 流程自动化编排。\n\n"
    "# 第 9 章：常见问题排查与诊断\n"
    "启动依赖、坐标投影异常、内存溢出分块优化与网络连通性排查。\n\n"
    "# 第 10 章：快捷键与操作速查表\n"
    "汇总全局工程、视口漫游、矢量编辑与影像配准快捷键速查表。\n"
  );
}
