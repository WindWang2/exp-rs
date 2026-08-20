// src/app/dialogs/spectral_library_dialog.cpp — Spectral Library Matching
#include "spectral_library_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

SpectralLibraryDialog::SpectralLibraryDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "光谱库匹配" ) );
  setMinimumWidth( 620 );
  setupUi();
}

void SpectralLibraryDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  QFrame *inputSec = SicnuUi::makeSection(
    this, tr( "待匹配光谱" ),
    tr( "使用光谱剖面面板（在图上点击）采集的像元光谱；也可保存回谱库。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );

  m_spectrumSummary = new QLabel( tr( "（未采集光谱）" ), inputSec );
  m_spectrumSummary->setWordWrap( true );
  form->addRow( tr( "当前剖面" ), m_spectrumSummary );

  m_libraryPathEdit = new QLineEdit( inputSec );
  m_libraryPathEdit->setObjectName( QStringLiteral( "spectralLibPathEdit" ) );
  m_libraryPathEdit->setPlaceholderText( tr( "选择光谱库 JSON 文件" ) );
  SicnuDialogHelp::tip( m_libraryPathEdit, tr(
    "光谱库文件（SpectralLibrary JSON）：命名光谱 + 可选波长栅格。"
    "也可在下方把当前谱保存进库。" ) );
  connect( m_libraryPathEdit, &QLineEdit::textChanged, this, [this]( const QString &text ) {
    m_libraryLoaded = ( !m_loadedPath.isEmpty() && text.trimmed() == m_loadedPath );
    m_saveButton->setEnabled( !m_values.isEmpty() && m_libraryLoaded );
  } );

  auto *browseButton = new QPushButton( tr( "浏览…" ), inputSec );
  browseButton->setObjectName( QStringLiteral( "spectralLibBrowseBtn" ) );
  SicnuDialogHelp::tip( browseButton, tr( "浏览并选择光谱库 JSON 文件" ) );
  connect( browseButton, &QPushButton::clicked, this, &SpectralLibraryDialog::browseLibrary );

  auto *pathRow = new QHBoxLayout();
  pathRow->addWidget( m_libraryPathEdit, 1 );
  pathRow->addWidget( browseButton );
  form->addRow( tr( "光谱库" ), pathRow );

  qobject_cast<QVBoxLayout *>( inputSec->layout() )->addLayout( form );
  mainLayout->addWidget( inputSec );

  m_matchButton = new QPushButton( tr( "匹配" ), this );
  SicnuUi::markPrimary( m_matchButton );
  m_matchButton->setObjectName( QStringLiteral( "spectralMatchBtn" ) );
  SicnuDialogHelp::tip( m_matchButton, tr( "运行 SAM / SID 光谱匹配算法，对光谱库中条目按相似度排序" ) );
  connect( m_matchButton, &QPushButton::clicked, this, &SpectralLibraryDialog::runMatch );

  m_saveButton = new QPushButton( tr( "保存当前谱到库" ), this );
  m_saveButton->setObjectName( QStringLiteral( "spectralSaveBtn" ) );
  SicnuDialogHelp::tip( m_saveButton, tr( "将当前采集的像元光谱曲线追加或保存到已加载的光谱库中" ) );
  connect( m_saveButton, &QPushButton::clicked, this, &SpectralLibraryDialog::saveCurrentToLibrary );

  auto *buttonRow = new QHBoxLayout();
  buttonRow->addWidget( m_matchButton );
  buttonRow->addWidget( m_saveButton );
  buttonRow->addStretch( 1 );
  mainLayout->addLayout( buttonRow );

  m_matchTable = new QTableWidget( this );
  m_matchTable->setObjectName( QStringLiteral( "spectralMatchTable" ) );
  SicnuDialogHelp::tip( m_matchTable, tr( "匹配结果列表：显示 SAM 夹角（越小越相似）与 SID 散度" ) );
  m_matchTable->setColumnCount( 5 );
  m_matchTable->setHorizontalHeaderLabels(
    { tr( "排名" ), tr( "名称" ), tr( "物质/类别" ), tr( "SAM (°)" ), tr( "SID" ) } );
  m_matchTable->horizontalHeader()->setStretchLastSection( true );
  m_matchTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
  m_matchTable->setSelectionBehavior( QAbstractItemView::SelectRows );
  m_matchTable->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
  m_matchTable->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
  m_matchTable->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::Stretch );
  mainLayout->addWidget( m_matchTable, 1 );

  m_statusLabel = SicnuUi::makeHintLabel( this, tr( "就绪" ) );
  mainLayout->addWidget( m_statusLabel );

  auto *helpButton = new QPushButton( tr( "帮助" ), this );
  SicnuDialogHelp::tip( helpButton, tr( "打开光谱库匹配帮助说明。" ) );
  connect( helpButton, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "spectral_library" ), windowTitle() );
  } );

  auto *closeButton = new QPushButton( tr( "关闭" ), this );
  SicnuDialogHelp::tip( closeButton, tr( "关闭对话框" ) );
  connect( closeButton, &QPushButton::clicked, this, &QDialog::accept );

  auto *closeRow = new QHBoxLayout();
  closeRow->addWidget( helpButton );
  closeRow->addStretch( 1 );
  closeRow->addWidget( closeButton );
  mainLayout->addLayout( closeRow );

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "spectral_library" ) );
}

void SpectralLibraryDialog::setSpectrum( const QVector<double> &values,
                                         const QVector<double> &wavelengths,
                                         const QVector<QString> &labels )
{
  m_values = values;
  m_wavelengths = wavelengths;
  m_labels = labels;
  updateSpectrumSummary();
}

void SpectralLibraryDialog::updateSpectrumSummary()
{
  if ( m_values.isEmpty() )
  {
    m_spectrumSummary->setText( tr( "（未采集光谱）" ) );
    m_matchButton->setEnabled( false );
    m_saveButton->setEnabled( false );
    return;
  }
  m_spectrumSummary->setText( tr( "%1 个波段%2" )
                                .arg( m_values.size() )
                                .arg( m_labels.isEmpty()
                                        ? QString()
                                        : tr( "（%1）" ).arg( m_labels.join( tr( ", " ) ) ) ) );
  m_matchButton->setEnabled( true );
  m_saveButton->setEnabled( m_libraryLoaded );
}

void SpectralLibraryDialog::browseLibrary()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "选择光谱库" ), QString(), tr( "Spectral Library JSON (*.json);;所有文件 (*)" ) );
  if ( !path.isEmpty() )
    m_libraryPathEdit->setText( path );
}

bool SpectralLibraryDialog::loadAndMatch( const QString &path, QString *errorMessage )
{
  SpectralLibrary::Library library;
  if ( !SpectralLibrary::Library::load( path, &library, errorMessage ) )
    return false;
  m_library = library;
  m_libraryLoaded = true;
  m_loadedPath = path;
  m_libraryPathEdit->setText( path );
  m_libraryPathEdit->setProperty( "loadedPath", path );
  m_saveButton->setEnabled( !m_values.isEmpty() );
  runMatch();
  return true;
}

void SpectralLibraryDialog::runMatch()
{
  if ( m_values.isEmpty() )
  {
    m_statusLabel->setText( tr( "请先在图上采集光谱剖面。" ) );
    return;
  }
  const QString path = m_libraryPathEdit->text().trimmed();
  if ( path.isEmpty() )
  {
    m_statusLabel->setText( tr( "请选择光谱库文件。" ) );
    return;
  }
  if ( !m_libraryLoaded || m_loadedPath != path )
  {
    QString errorMessage;
    SpectralLibrary::Library library;
    if ( !SpectralLibrary::Library::load( path, &library, &errorMessage ) )
    {
      m_statusLabel->setText( tr( "加载失败：%1" ).arg( errorMessage ) );
      return;
    }
    m_library = library;
    m_libraryLoaded = true;
    m_loadedPath = path;
    m_libraryPathEdit->setProperty( "loadedPath", path );
    m_saveButton->setEnabled( !m_values.isEmpty() );
  }

  std::vector<float> spectrum;
  spectrum.reserve( m_values.size() );
  for ( double v : m_values )
    spectrum.push_back( static_cast<float>( v ) );

  std::vector<float> wavelengths;
  if ( m_wavelengths.size() == m_values.size() )
  {
    wavelengths.reserve( m_wavelengths.size() );
    for ( double w : m_wavelengths )
      wavelengths.push_back( static_cast<float>( w ) );
  }

  const auto scores = SpectralLibrary::matchSpectrum( spectrum, wavelengths, m_library );
  m_matchTable->setRowCount( static_cast<int>( scores.size() ) );
  m_tableRowCount = static_cast<int>( scores.size() );
  for ( int row = 0; row < m_tableRowCount; ++row )
  {
    const SpectralLibrary::MatchScore &score = scores[row];
    auto *rankItem = new QTableWidgetItem( QString::number( row + 1 ) );
    auto *nameItem = new QTableWidgetItem( score.name );
    auto *materialItem = new QTableWidgetItem( score.material );
    auto *angleItem = new QTableWidgetItem(
      std::isnan( score.angleDegrees )
        ? tr( "—" )
        : QString::number( score.angleDegrees, 'f', 3 ) );
    auto *sidItem = new QTableWidgetItem(
      std::isnan( score.divergence )
        ? tr( "—" )
        : QString::number( score.divergence, 'f', 4 ) );
    m_matchTable->setItem( row, 0, rankItem );
    m_matchTable->setItem( row, 1, nameItem );
    m_matchTable->setItem( row, 2, materialItem );
    m_matchTable->setItem( row, 3, angleItem );
    m_matchTable->setItem( row, 4, sidItem );
  }

  const int comparable = m_tableRowCount;
  m_statusLabel->setText(
    tr( "匹配完成：%1 个可比条目（SAM 升序）。谱库共 %2 个条目。"
        "未匹配条目通常因波段数不一致且缺少波长栅格。"
        "带波长栅格的条目已自动重采样后匹配。" )
      .arg( comparable )
      .arg( m_library.entries.size() ) );
}

void SpectralLibraryDialog::saveCurrentToLibrary()
{
  if ( m_values.isEmpty() )
    return;

  const QString path = m_libraryPathEdit->text().trimmed();
  if ( path.isEmpty() )
  {
    m_statusLabel->setText( tr( "请选择光谱库文件。" ) );
    return;
  }
  if ( !m_libraryLoaded || m_loadedPath != path )
  {
    QString errorMessage;
    SpectralLibrary::Library library;
    if ( !SpectralLibrary::Library::load( path, &library, &errorMessage ) )
    {
      m_statusLabel->setText( tr( "加载失败：%1" ).arg( errorMessage ) );
      return;
    }
    m_library = library;
    m_libraryLoaded = true;
    m_loadedPath = path;
    m_libraryPathEdit->setProperty( "loadedPath", path );
  }

  SpectralLibrary::Entry entry;
  entry.name = QStringLiteral( "profile_%1" ).arg( m_library.entries.size() + 1 );
  entry.material = tr( "未命名" );
  entry.source = tr( "光谱剖面面板" );
  entry.spectrum.reserve( m_values.size() );
  for ( double v : m_values )
    entry.spectrum.push_back( static_cast<float>( v ) );
  if ( m_wavelengths.size() == m_values.size() )
  {
    entry.wavelengths.reserve( m_wavelengths.size() );
    for ( double w : m_wavelengths )
      entry.wavelengths.push_back( static_cast<float>( w ) );
  }

  m_library.entries.append( entry );
  QString errorMessage;
  if ( !m_library.save( path, &errorMessage ) )
  {
    m_statusLabel->setText( tr( "保存失败：%1" ).arg( errorMessage ) );
    return;
  }
  m_statusLabel->setText( tr( "已保存条目“%1”到 %2" ).arg( entry.name, path ) );
}

void SpectralLibraryDialog::showEvent( QShowEvent *event )
{
  QDialog::showEvent( event );
  updateSpectrumSummary();
}
