#include "rs_georef_params_panel.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "dialogs/dialog_help_catalog.h"
#include "qgsprojectionselectionwidget.h"
#include "rs_rms_scatter_widget.h"

namespace
{
  void setHelp( QWidget *w, const QString &tip )
  {
    if ( !w )
      return;
    w->setToolTip( tip );
    w->setWhatsThis( tip );
    w->setStatusTip( tip );
  }

  QLabel *formLabel( const QString &text, const QString &tip, QWidget *parent )
  {
    auto *lbl = new QLabel( text, parent );
    setHelp( lbl, tip );
    return lbl;
  }

  QFrame *makeSectionFrame( const QString &title, QWidget *parent, const QString &sectionTip = {} )
  {
    auto *frame = new QFrame( parent );
    frame->setObjectName( QStringLiteral( "rsParamSection" ) );
    frame->setFrameShape( QFrame::StyledPanel );
    auto *lay = new QVBoxLayout( frame );
    lay->setContentsMargins( 8, 6, 8, 8 );
    lay->setSpacing( 6 );
    auto *header = new QLabel( title, frame );
    header->setObjectName( QStringLiteral( "rsParamSectionTitle" ) );
    QFont f = header->font();
    f.setBold( true );
    header->setFont( f );
    if ( !sectionTip.isEmpty() )
    {
      header->setToolTip( sectionTip );
      header->setWhatsThis( sectionTip );
      header->setStatusTip( sectionTip );
      frame->setToolTip( sectionTip );
      frame->setWhatsThis( sectionTip );
    }
    lay->addWidget( header );
    return frame;
  }
}

RsGeorefParamsPanel::RsGeorefParamsPanel( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsGeorefParamsPanel" ) );
  setMinimumWidth( 320 );
  setHelp( this, SicnuDialogHelp::shortForTool(
             QStringLiteral( "georef_params" ),
             tr( "校正参数面板" ) ) );

  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 6, 6, 6, 6 );
  root->setSpacing( 8 );

  // ---- Help banner ----
  {
    auto *banner = new QFrame( this );
    banner->setObjectName( QStringLiteral( "rsGeorefParamsHelpBanner" ) );
    banner->setFrameShape( QFrame::StyledPanel );
    banner->setStyleSheet(
      QStringLiteral( "QFrame#rsGeorefParamsHelpBanner {"
                      "  background:#f6f8fa; border:1px solid #d0d7de; border-radius:4px; }" ) );
    auto *bl = new QHBoxLayout( banner );
    bl->setContentsMargins( 8, 6, 8, 6 );
    auto *sum = new QLabel(
      SicnuDialogHelp::shortForTool( QStringLiteral( "georef_params" ),
                                     tr( "校正参数：变换 / 重采样 / 残差 / CRS / 输出" ) ),
      banner );
    sum->setWordWrap( true );
    setHelp( sum, tr(
      "本面板控制几何校正的全部写出参数。\n"
      "悬停各分区标题与控件可看详细说明；点「参数说明」查看完整文档。" ) );
    auto *helpBtn = new QPushButton( tr( "参数说明" ), banner );
    helpBtn->setObjectName( QStringLiteral( "rsGeorefParamsHelpBtn" ) );
    helpBtn->setFlat( false );
    setHelp( helpBtn, tr( "打开「校正参数」完整说明（变换方法、点数、重采样、RMS、CRS、输出）。" ) );
    connect( helpBtn, &QPushButton::clicked, this, [this]() {
      SicnuDialogHelp::showToolHelp( this, QStringLiteral( "georef_params" ),
                                     tr( "校正参数面板" ) );
    } );
    bl->addWidget( sum, 1 );
    bl->addWidget( helpBtn, 0, Qt::AlignTop );
    root->addWidget( banner );
  }

  // ---- Section 1: 坐标变换 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "坐标变换" ), this,
      tr(
        "【坐标变换】用 GCP 拟合「源影像坐标 → 目标坐标」的几何模型。\n"
        "不同方法所需最少点数不同；实际点数不足时无法可靠拟合，「运行」会禁用。\n"
        "同景配准一般用 Linear 或 一次多项式即可；复杂畸变再用高阶/TPS。" ) );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mTransformCombo = new QComboBox( sec );
    mTransformCombo->setObjectName( QStringLiteral( "rsTransformCombo" ) );
    using TM = QgsGcpTransformerInterface::TransformMethod;
    const QVector<QPair<TM, QString>> methods = {
      { TM::Linear, tr( "Linear (线性)" ) },
      { TM::Helmert, tr( "Helmert" ) },
      { TM::PolynomialOrder1, tr( "Polynomial Order 1 (一次多项式)" ) },
      { TM::PolynomialOrder2, tr( "Polynomial Order 2 (二次多项式)" ) },
      { TM::PolynomialOrder3, tr( "Polynomial Order 3 (三次多项式)" ) },
      { TM::ThinPlateSpline, tr( "Thin Plate Spline (薄板样条)" ) },
      { TM::Projective, tr( "Projective (透视)" ) },
      { TM::RpcPhysical, tr( "RPC Physical (RFM 物理模型)" ) },
    };
    for ( const auto &m : methods )
      mTransformCombo->addItem( m.second, QVariant::fromValue( static_cast<int>( m.first ) ) );

    // Ensure the combo view is a QListView so we can hide individual rows
    // when toggling RPC mode (see setRpcMode()).
    mTransformCombo->setView( new QListView( mTransformCombo ) );

    connect( mTransformCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, [this]( int ) { emit transformMethodChanged(); } );

    setHelp( mTransformCombo, tr(
      "变换方法（几何模型）：\n"
      "• Linear（线性，≥2 点）：平移+缩放，同景/近似共线时常用\n"
      "• Helmert（≥2 点）：相似变换（旋转+统一缩放）\n"
      "• 一次多项式（≥3 点）：仿射，纠正旋转/剪切\n"
      "• 二次/三次多项式（≥6/10 点）：弯曲变形，阶数高易过拟合\n"
      "• TPS 薄板样条：局部变形强，GCP 宜均匀\n"
      "• Projective：透视（扫描图、倾斜摄影）\n"
      "• RPC Physical：传感器 RPC，仅 Image→Map，需元数据与可选 DEM\n\n"
      "提示：点数刚好等于最少点数时 DOF=0，残差会接近 0，不能说明精度好，应多采点。" ) );
    form->addRow( formLabel( tr( "方法" ),
                              tr( "几何变换模型类型。悬停下拉框查看各方法说明与最少点数。" ), sec ),
                 mTransformCombo );

    mMinPtsLabel = new QLabel( tr( "—" ), sec );
    mActualPtsLabel = new QLabel( tr( "—" ), sec );
    mDofLabel = new QLabel( tr( "—" ), sec );
    mMinPtsLabel->setObjectName( QStringLiteral( "rsMinPtsLabel" ) );
    mActualPtsLabel->setObjectName( QStringLiteral( "rsActualPtsLabel" ) );
    mDofLabel->setObjectName( QStringLiteral( "rsDofLabel" ) );
    setHelp( mMinPtsLabel, tr(
      "最少点数：当前变换方法要求的「已启用」GCP 下限。\n"
      "例如三次多项式通常约 10 点。未达下限时不能可靠拟合。" ) );
    setHelp( mActualPtsLabel, tr(
      "实际可用点数：GCP 表中勾选「启用」的控制点个数。\n"
      "只有启用的点参与拟合与 RMS 计算。" ) );
    setHelp( mDofLabel, tr(
      "自由度 DOF = 实际可用点数 − 最少点数。\n"
      "• DOF < 0：点数不够，无法拟合\n"
      "• DOF = 0：刚好定解，残差会被「拟合光」，几乎总是 0，无统计意义\n"
      "• DOF > 0：可过约束，用 RMS 评估精度；宜再多采均匀分布的点" ) );

    form->addRow( formLabel( tr( "最少点数" ),
                              tr( "方法所需最少启用 GCP 数。" ), sec ), mMinPtsLabel );
    form->addRow( formLabel( tr( "实际可用点数" ),
                              tr( "已启用并参与拟合的 GCP 数。" ), sec ), mActualPtsLabel );
    form->addRow( formLabel( tr( "自由度 DOF" ),
                              tr( "实际点数减最少点数。>0 才能用残差评估。" ), sec ), mDofLabel );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 2: 重采样 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "重采样" ), this,
      tr(
        "【重采样】将源影像按变换模型「扭曲」到目标网格时的像元插值方式。\n"
        "只影响输出影像的平滑/锐利程度，不改变 GCP 几何拟合本身。" ) );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mResamplingCombo = new QComboBox( sec );
    mResamplingCombo->setObjectName( QStringLiteral( "rsResamplingCombo" ) );
    using RM = QgsImageWarper::ResamplingMethod;
    mResamplingCombo->addItem( tr( "Nearest Neighbour" ), QVariant::fromValue( static_cast<int>( RM::NearestNeighbour ) ) );
    mResamplingCombo->addItem( tr( "Bilinear" ), QVariant::fromValue( static_cast<int>( RM::Bilinear ) ) );
    mResamplingCombo->addItem( tr( "Cubic" ), QVariant::fromValue( static_cast<int>( RM::Cubic ) ) );
    mResamplingCombo->addItem( tr( "Cubic Spline" ), QVariant::fromValue( static_cast<int>( RM::CubicSpline ) ) );
    mResamplingCombo->addItem( tr( "Lanczos" ), QVariant::fromValue( static_cast<int>( RM::Lanczos ) ) );
    connect( mResamplingCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, [this]( int ) { emit resamplingMethodChanged(); } );
    setHelp( mResamplingCombo, tr(
      "重采样算法（写出栅格时）：\n"
      "• Nearest Neighbour：最近邻，不混合邻域，分类/整型标签首选\n"
      "• Bilinear：双线性，连续灰度/多光谱常用，速度与质量均衡\n"
      "• Cubic：三次卷积，更平滑，边缘略糊\n"
      "• Cubic Spline / Lanczos：更高阶，更锐/更慢，慎用于定量\n\n"
      "光学目视：Bilinear 或 Cubic；分类图：Nearest。" ) );
    form->addRow( formLabel( tr( "算法" ), tr( "像元插值方法。" ), sec ), mResamplingCombo );

    mPixelSize = new QDoubleSpinBox( sec );
    mPixelSize->setObjectName( QStringLiteral( "rsPixelSize" ) );
    mPixelSize->setRange( 0.0, 1.0e9 );
    mPixelSize->setDecimals( 6 );
    mPixelSize->setValue( 0.0 );
    mPixelSize->setSpecialValueText( tr( "auto" ) );
    setHelp( mPixelSize, tr(
      "输出像元大小（目标 CRS 的地面单位，如米）。\n"
      "• auto（0）：由引擎按输入/参考估计\n"
      "• 手动：如 30 表示 30 m 分辨率（UTM 下）\n"
      "I2I 对齐参考时常用参考分辨率或 auto。" ) );
    form->addRow( formLabel( tr( "输出像元大小" ),
                              tr( "目标网格分辨率；auto=自动。" ), sec ), mPixelSize );

    mOutputExtent = new QLineEdit( sec );
    mOutputExtent->setObjectName( QStringLiteral( "rsOutputExtent" ) );
    mOutputExtent->setReadOnly( true );
    mOutputExtent->setText( tr( "auto · ref" ) );
    setHelp( mOutputExtent, tr(
      "输出地理范围（只读预览）。\n"
      "auto · ref：按参考/变换结果自动确定范围，一般无需改。" ) );
    form->addRow( formLabel( tr( "输出范围" ), tr( "结果覆盖的地图范围。" ), sec ), mOutputExtent );

    mBackground = new QSpinBox( sec );
    mBackground->setObjectName( QStringLiteral( "rsBackground" ) );
    mBackground->setRange( 0, 65535 );
    mBackground->setValue( 0 );
    setHelp( mBackground, tr(
      "背景/填充值：扭曲后无源数据覆盖的像元写入此值。\n"
      "常用 0；若 0 是有效 DN，可改为如 65535 并在结果中设 NoData。" ) );
    connect( mBackground, QOverload<int>::of( &QSpinBox::valueChanged ),
             this, [this]( int v ) { emit backgroundValueChanged( v ); } );
    form->addRow( formLabel( tr( "背景值" ), tr( "空洞填充像元值。" ), sec ), mBackground );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 3: RMS 误差分布 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "RMS 误差分布" ), this,
      tr(
        "【RMS 误差】启用 GCP 上「预测位置 − 观测位置」的均方根。\n"
        "单位一般为源影像像元 (px)。同名点选得准、模型合适时 RMS 应较小。\n"
        "DOF=0 时残差会被拟合到接近 0，不能代表真实精度——请多采点。" ) );
    auto *vbox = new QVBoxLayout();
    vbox->setContentsMargins( 0, 0, 0, 0 );

    mScatter = new RsRmsScatterWidget( sec );
    setHelp( mScatter, tr(
      "残差散点图：\n"
      "• 横轴 ≈ ΔX（列方向残差）\n"
      "• 纵轴 ≈ ΔY（行方向残差）\n"
      "点应靠近原点且大致各向均匀。离群点：检查是否取错同名地物，或在 GCP 表禁用该点。" ) );
    vbox->addWidget( mScatter, 0, Qt::AlignHCenter );

    auto *grid = new QFormLayout();
    grid->setContentsMargins( 0, 0, 0, 0 );
    mXRms = new QLabel( tr( "—" ), sec );
    mYRms = new QLabel( tr( "—" ), sec );
    mTotalRms = new QLabel( tr( "—" ), sec );
    mMaxRms = new QLabel( tr( "—" ), sec );
    mXRms->setObjectName( QStringLiteral( "rsXRmsLabel" ) );
    mYRms->setObjectName( QStringLiteral( "rsYRmsLabel" ) );
    mTotalRms->setObjectName( QStringLiteral( "rsTotalRmsLabel" ) );
    mMaxRms->setObjectName( QStringLiteral( "rsMaxRmsLabel" ) );
    setHelp( mXRms, tr( "X 方向残差的均方根（像元）。" ) );
    setHelp( mYRms, tr( "Y 方向残差的均方根（像元）。" ) );
    setHelp( mTotalRms, tr(
      "Total RMS：所有启用 GCP 残差模长的均方根。\n"
      "目视同景配准：通常希望数像素级；若数百～数千需检查 CRS/Sync zoom/取点。" ) );
    setHelp( mMaxRms, tr(
      "最大残差及对应 GCP 编号。优先检查该点是否取错或影像边缘畸变。" ) );
    grid->addRow( formLabel( tr( "X RMS" ), tr( "X 向残差 RMS。" ), sec ), mXRms );
    grid->addRow( formLabel( tr( "Y RMS" ), tr( "Y 向残差 RMS。" ), sec ), mYRms );
    grid->addRow( formLabel( tr( "Total RMS" ), tr( "总残差 RMS。" ), sec ), mTotalRms );
    grid->addRow( formLabel( tr( "最大残差" ), tr( "最差的一个 GCP。" ), sec ), mMaxRms );

    // Task 11.5.5 — before/after RMS readout for RPC linear-bias refinement.
    // Empty until the main window calls setRefinementRms().
    mRmsBefore = new QLabel( QString(), sec );
    mRmsBefore->setObjectName( QStringLiteral( "rsRmsBefore" ) );
    mRmsAfter = new QLabel( QString(), sec );
    mRmsAfter->setObjectName( QStringLiteral( "rsRmsAfter" ) );
    setHelp( mRmsBefore, tr( "RPC 精化前 RMS（有 ≥3 个 GCP 时显示）。" ) );
    setHelp( mRmsAfter, tr( "RPC 线性偏差精化后 RMS；绿字表示精化改善。" ) );

    vbox->addItem( grid );
    vbox->addWidget( mRmsBefore );
    vbox->addWidget( mRmsAfter );
    sec->layout()->addItem( vbox );
    root->addWidget( sec );
  }

  // ---- Section 4: 坐标系 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "坐标系" ), this,
      tr(
        "【坐标系】目标 CRS 决定输出 GeoTIFF 的投影，并参与 GCP 目标坐标解释。\n"
        "I2I：通常与参考影像 CRS 一致（加载参考后会自动对齐）。\n"
        "I2M：Map 画布会尽量跟随目标 CRS 显示主工程图层。" ) );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mSrcCrsLabel = new QLabel( tr( "—" ), sec );
    mSrcCrsLabel->setObjectName( QStringLiteral( "rsSrcCrsLabel" ) );
    setHelp( mSrcCrsLabel, tr(
      "源影像 (Warp) 的坐标系。未定义时显示 —。\n"
      "取点坐标以图层 CRS 为准。" ) );

    // Task 11.5.1 — real CRS picker replaces the hard-coded EPSG:32650 label.
    mCrsWidget = new QgsProjectionSelectionWidget( sec );
    mCrsWidget->setObjectName( QStringLiteral( "rsCrsWidget" ) );
    setHelp( mCrsWidget, tr(
      "目标 CRS：校正结果与拟合所用目标坐标系。\n"
      "常见：WGS 84 / UTM zone xxN、CGCS2000 高斯投影等。\n"
      "I2I 加载参考影像后会尽量自动设为参考 CRS。" ) );

    // Restore last user choice (default to EPSG:32650 to preserve previous
    // behaviour from Task 11.4 when no setting exists yet).
    {
      const QString lastAuthid = QSettings()
                                   .value( QStringLiteral( "Georeferencer/lastDestCrs" ),
                                           QStringLiteral( "EPSG:32650" ) )
                                   .toString();
      QgsCoordinateReferenceSystem saved;
      if ( !lastAuthid.isEmpty() )
        saved.createFromOgcWmsCrs( lastAuthid );
      if ( saved.isValid() )
        mCrsWidget->setCrs( saved );
    }

    connect( mCrsWidget, &QgsProjectionSelectionWidget::crsChanged, this,
             [this]( const QgsCoordinateReferenceSystem &crs ) {
               QSettings().setValue( QStringLiteral( "Georeferencer/lastDestCrs" ),
                                     crs.authid() );
               if ( mProjNameLabel )
                 mProjNameLabel->setText( crs.description().isEmpty() ? crs.authid()
                                                                     : crs.description() );
               emit destCrsChanged();
             } );

    mProjNameLabel = new QLabel( sec );
    mProjNameLabel->setObjectName( QStringLiteral( "rsProjNameLabel" ) );
    {
      const QgsCoordinateReferenceSystem cur = mCrsWidget->crs();
      mProjNameLabel->setText( cur.description().isEmpty() ? cur.authid() : cur.description() );
    }

    form->addRow( formLabel( tr( "源 CRS" ), tr( "源影像坐标系。" ), sec ), mSrcCrsLabel );
    form->addRow( formLabel( tr( "目标 CRS" ), tr( "结果与拟合目标坐标系。" ), sec ), mCrsWidget );
    form->addRow( formLabel( tr( "投影名" ), tr( "目标 CRS 的可读名称。" ), sec ), mProjNameLabel );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 5: 输出 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "输出" ), this,
      tr(
        "【输出】校正后的 GeoTIFF 保存路径。\n"
        "必须填写有效路径后，工具栏「运行」才会启用（且 GCP 数量与拟合需满足条件）。\n"
        "任务列表会记录该路径，完成后可加载到主工程。" ) );
    auto *row = new QHBoxLayout();
    row->setContentsMargins( 0, 0, 0, 0 );

    mOutputPath = new QLineEdit( sec );
    mOutputPath->setObjectName( QStringLiteral( "rsOutputPath" ) );
    mOutputPath->setPlaceholderText( tr( "/path/to/output.tif" ) );
    connect( mOutputPath, &QLineEdit::textChanged,
             this, [this]( const QString &s ) { emit outputPathChanged( s ); } );
    setHelp( mOutputPath, tr(
      "输出文件完整路径，建议使用 .tif / .tiff。\n"
      "目录需可写；同名文件可能被覆盖（视任务实现）。" ) );

    mBrowseBtn = new QPushButton( tr( "Browse…" ), sec );
    mBrowseBtn->setObjectName( QStringLiteral( "rsBrowseOutputBtn" ) );
    connect( mBrowseBtn, &QPushButton::clicked, this, &RsGeorefParamsPanel::onBrowseOutput );
    setHelp( mBrowseBtn, tr( "浏览选择输出文件位置。" ) );

    row->addWidget( mOutputPath, 1 );
    row->addWidget( mBrowseBtn );
    sec->layout()->addItem( row );
    root->addWidget( sec );
  }

  // ---- Section 6: DEM (RPC mode only) ----
  {
    mDemSection = makeSectionFrame(
      tr( "DEM (RPC 模式)" ), this,
      tr(
        "【DEM】仅当变换方法为 RPC Physical 时显示。\n"
        "可选 DEM 改善 RPC 投影高程；Z 偏移为相对 DEM 的米制修正。" ) );
    mDemSection->setObjectName( QStringLiteral( "rsDemSection" ) );

    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    auto *demRow = new QHBoxLayout();
    demRow->setContentsMargins( 0, 0, 0, 0 );
    mDemPath = new QLineEdit( mDemSection );
    mDemPath->setObjectName( QStringLiteral( "rsDemPath" ) );
    mDemPath->setPlaceholderText( tr( "/path/to/dem.tif (可选)" ) );
    setHelp( mDemPath, tr( "数字高程模型路径（可选）。用于 RPC 高度相关投影。" ) );
    mDemBrowseBtn = new QPushButton( tr( "Browse…" ), mDemSection );
    mDemBrowseBtn->setObjectName( QStringLiteral( "rsDemBrowseBtn" ) );
    connect( mDemBrowseBtn, &QPushButton::clicked, this, [this]() {
      const QString path = QFileDialog::getOpenFileName(
        this,
        tr( "选择 DEM 文件" ),
        mDemPath->text(),
        tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
      if ( !path.isEmpty() )
        setDemPath( path );
    } );
    setHelp( mDemBrowseBtn, tr( "选择 DEM 栅格文件。" ) );
    demRow->addWidget( mDemPath, 1 );
    demRow->addWidget( mDemBrowseBtn );
    form->addRow( tr( "DEM 路径" ), demRow );

    mDemZOffset = new QDoubleSpinBox( mDemSection );
    mDemZOffset->setObjectName( QStringLiteral( "rsDemZOffset" ) );
    mDemZOffset->setRange( -10000.0, 10000.0 );
    mDemZOffset->setDecimals( 2 );
    mDemZOffset->setValue( 0.0 );
    mDemZOffset->setSuffix( tr( " m" ) );
    // Task 11.5.4 — propagate spin-box edits so the main window can
    // recompute the RPC fit (and the warp pipeline picks up RPC_HEIGHT).
    connect( mDemZOffset, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, [this]( double ) { emit demZOffsetChanged(); } );
    setHelp( mDemZOffset, tr( "相对 DEM 的高程偏移（米），传入 RPC_HEIGHT 选项。" ) );
    form->addRow( tr( "高程偏移" ), mDemZOffset );

    mDemSection->layout()->addItem( form );
    mDemSection->setVisible( false );
    root->addWidget( mDemSection );
  }

  // Default profile is ImageToImage — hide RPC until I2M setProfile().
  applyProfileToMethodCombo();

  root->addStretch( 1 );
}

QgsGcpTransformerInterface::TransformMethod RsGeorefParamsPanel::transformMethod() const
{
  const int v = mTransformCombo->currentData().toInt();
  return static_cast<QgsGcpTransformerInterface::TransformMethod>( v );
}

QgsImageWarper::ResamplingMethod RsGeorefParamsPanel::resamplingMethod() const
{
  const int v = mResamplingCombo->currentData().toInt();
  return static_cast<QgsImageWarper::ResamplingMethod>( v );
}

QString RsGeorefParamsPanel::outputPath() const
{
  return mOutputPath->text().trimmed();
}

void RsGeorefParamsPanel::setTransformMethod( QgsGcpTransformerInterface::TransformMethod m )
{
  if ( !mTransformCombo )
    return;
  const int idx = mTransformCombo->findData( QVariant::fromValue( static_cast<int>( m ) ) );
  if ( idx >= 0 )
    mTransformCombo->setCurrentIndex( idx );
}

void RsGeorefParamsPanel::setResamplingMethod( QgsImageWarper::ResamplingMethod m )
{
  if ( !mResamplingCombo )
    return;
  const int idx = mResamplingCombo->findData( QVariant::fromValue( static_cast<int>( m ) ) );
  if ( idx >= 0 )
    mResamplingCombo->setCurrentIndex( idx );
}

void RsGeorefParamsPanel::setOutputPath( const QString &path )
{
  if ( mOutputPath )
    mOutputPath->setText( path );
}

void RsGeorefParamsPanel::setDemPath( const QString &path )
{
  if ( mDemPath )
    mDemPath->setText( path );
}

QgsCoordinateReferenceSystem RsGeorefParamsPanel::destCrs() const
{
  // Task 11.5.1 — picker is the source of truth. Fall back to EPSG:32650 if
  // the widget was never constructed (defensive — shouldn't happen).
  if ( mCrsWidget )
    return mCrsWidget->crs();
  return QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:32650" ) );
}

void RsGeorefParamsPanel::setDestCrs( const QgsCoordinateReferenceSystem &crs )
{
  if ( !mCrsWidget )
    return;
  // QgsProjectionSelectionWidget emits crsChanged() when its current CRS
  // actually changes; our slot persists + emits destCrsChanged(). If the
  // incoming CRS equals the current one no signal would fire, so we fan it
  // out unconditionally here to honour the "setter triggers signal" contract
  // the test (and main-window recomputeFit wiring) depends on.
  const bool sameCrs = ( mCrsWidget->crs() == crs );
  mCrsWidget->setCrs( crs );
  if ( sameCrs )
  {
    QSettings().setValue( QStringLiteral( "Georeferencer/lastDestCrs" ), crs.authid() );
    if ( mProjNameLabel )
      mProjNameLabel->setText( crs.description().isEmpty() ? crs.authid() : crs.description() );
    emit destCrsChanged();
  }
}

double RsGeorefParamsPanel::outputPixelSize() const
{
  return mPixelSize->value();
}

bool RsGeorefParamsPanel::isDemSectionVisible() const
{
  // Use !isHidden() rather than isVisible(): the latter is false when the
  // window has not yet been shown, but the DEM-section's own visibility
  // intent is independently observable.  Tests construct the window without
  // showing it, so we report the local (unrealized) visibility state.
  return mDemSection && !mDemSection->isHidden();
}

QString RsGeorefParamsPanel::demPath() const
{
  return mDemPath ? mDemPath->text().trimmed() : QString();
}

double RsGeorefParamsPanel::demZOffset() const
{
  return mDemZOffset ? mDemZOffset->value() : 0.0;
}

void RsGeorefParamsPanel::setDemZOffset( double z )
{
  if ( mDemZOffset )
    mDemZOffset->setValue( z );
}

int RsGeorefParamsPanel::backgroundValue() const
{
  return mBackground ? mBackground->value() : 0;
}

void RsGeorefParamsPanel::setBackgroundValue( int v )
{
  if ( mBackground )
    mBackground->setValue( v );
}

void RsGeorefParamsPanel::setProfile( Profile p )
{
  mProfile = p;
  applyProfileToMethodCombo();
  if ( mProfile == Profile::ImageToImage )
  {
    setRpcMode( false );
  }
  else
  {
    setRpcMode( transformMethod() ==
                QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
  }
}

void RsGeorefParamsPanel::applyProfileToMethodCombo()
{
  if ( !mTransformCombo )
    return;

  auto *view = qobject_cast<QListView *>( mTransformCombo->view() );
  const int rpcIdx = mTransformCombo->findData(
    QVariant::fromValue(
      static_cast<int>( QgsGcpTransformerInterface::TransformMethod::RpcPhysical ) ) );

  // I2I: hide RPC row. I2M: show every method (including RPC).
  for ( int i = 0; i < mTransformCombo->count(); ++i )
  {
    const auto m = static_cast<QgsGcpTransformerInterface::TransformMethod>(
      mTransformCombo->itemData( i ).toInt() );
    const bool isRpc = ( m == QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
    const bool hide = ( mProfile == Profile::ImageToImage && isRpc );
    if ( view )
      view->setRowHidden( i, hide );
  }

  if ( mProfile == Profile::ImageToImage
       && transformMethod() == QgsGcpTransformerInterface::TransformMethod::RpcPhysical )
  {
    const int linIdx = mTransformCombo->findData(
      QVariant::fromValue(
        static_cast<int>( QgsGcpTransformerInterface::TransformMethod::Linear ) ) );
    if ( linIdx >= 0 )
      mTransformCombo->setCurrentIndex( linIdx );
  }

  Q_UNUSED( rpcIdx )
}

void RsGeorefParamsPanel::setRpcMode( bool on )
{
  // DEM section only — method combo is controlled by setProfile().
  if ( mDemSection )
    mDemSection->setVisible( on );
  // RPC warp (#363): GDAL RPC transformer outputs WGS84 degrees but the warp
  // path stamped the panel destCrs directly — force to EPSG:4326 and disable
  // picker while in RPC mode to avoid silent Earth-scale misplacement.
  if ( mCrsWidget )
  {
    mCrsWidget->setEnabled( !on );
    if ( on )
    {
      const QgsCoordinateReferenceSystem wgs84( QStringLiteral( "EPSG:4326" ) );
      if ( mCrsWidget->crs() != wgs84 )
        setDestCrs( wgs84 );
      mCrsWidget->setToolTip( tr( "RPC 模式下目标 CRS 固定为 EPSG:4326（WGS84 经纬度，RPC 输出空间）；如需投影请在校正后另行重投影" ) );
    }
    else
    {
      mCrsWidget->setToolTip( tr( "目标 CRS：校正结果与拟合所用目标坐标系。\n常见：WGS 84 / UTM zone xxN、CGCS2000 高斯投影等。\nI2I 加载参考影像后会尽量自动设为参考 CRS。" ) );
    }
  }
}

void RsGeorefParamsPanel::setRmsValues( int /*total*/, int /*enabled*/,
                                        double rmsPx, double xRms, double yRms,
                                        double maxRms, int maxRmsRowId )
{
  auto fmt = []( double v ) { return QString::number( v, 'f', 3 ); };
  mXRms->setText( fmt( xRms ) + tr( " px" ) );
  mYRms->setText( fmt( yRms ) + tr( " px" ) );
  mTotalRms->setText( fmt( rmsPx ) + tr( " px" ) );
  if ( maxRmsRowId >= 0 )
    mMaxRms->setText( tr( "%1 px (行 #%2)" ).arg( fmt( maxRms ) ).arg( maxRmsRowId + 1 ) );
  else
    mMaxRms->setText( fmt( maxRms ) + tr( " px" ) );
}

void RsGeorefParamsPanel::setResidualScatter( const QVector<QPointF> &dxdy )
{
  if ( mScatter )
    mScatter->setResiduals( dxdy );
}

void RsGeorefParamsPanel::setRefinementRms( double before, double after )
{
  if ( mRmsBefore )
    mRmsBefore->setText( tr( "精化前 RMS: %1 px" ).arg( before, 0, 'f', 3 ) );
  if ( mRmsAfter )
  {
    mRmsAfter->setText( tr( "精化后 RMS: %1 px" ).arg( after, 0, 'f', 3 ) );
    mRmsAfter->setStyleSheet( after < before
                                ? QStringLiteral( "color: #208830;" )
                                : QStringLiteral( "color: #5f6b7a;" ) );
  }
}

void RsGeorefParamsPanel::clearRefinementRms()
{
  if ( mRmsBefore )
    mRmsBefore->setText( tr( "精化前 RMS: —" ) );
  if ( mRmsAfter )
  {
    mRmsAfter->setText( tr( "精化后 RMS: —" ) );
    mRmsAfter->setStyleSheet( QString() );
  }
}

void RsGeorefParamsPanel::setMinimumGcpCount( int n )
{
  mMinPtsLabel->setText( QString::number( n ) );
}

void RsGeorefParamsPanel::setActualGcpCount( int n )
{
  mActualPtsLabel->setText( QString::number( n ) );

  // Update DOF: actual - min for current method. If actual < min, DOF = 0.
  bool ok = false;
  const int minN = mMinPtsLabel->text().toInt( &ok );
  if ( ok )
  {
    const int dof = std::max( 0, n - minN );
    mDofLabel->setText( QString::number( dof ) );
  }
}

void RsGeorefParamsPanel::onBrowseOutput()
{
  const QString path = QFileDialog::getSaveFileName(
    this,
    tr( "选择输出 GeoTIFF" ),
    mOutputPath->text(),
    tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
  if ( path.isEmpty() )
    return;
  setOutputPath( path );
}
